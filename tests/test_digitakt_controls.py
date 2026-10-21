# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise compiled Digitakt controls through native MIDI and panel input."""
import argparse
import contextlib
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--os', choices=('1.53', '1.54'), required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    sys.path.insert(0, str(ROOT / 'vendor/digiemu'))
    from emu import panel
    from emu.bootstrap import first_run
    from emu.fwcheck import prepare
    from emu.harness import call
    from emu.longrun import never_fake_set_posts
    from emu.midi import MidiIn
    from emu.session import Session
    from unicorn.m68k_const import UC_M68K_REG_A2, UC_M68K_REG_D2, UC_M68K_REG_PC

    core = json.loads((ROOT / 'vendor/elekloader/mods/core/mod.json').read_text())
    symbols = {k: int(v, 0) for k, v in (core['defsym'] | core.get('ports', {}).get(args.os, {}).get('defsym', {})).items()}
    report = dict(firmware_sha256=hashlib.sha256(args.firmware.read_bytes()).hexdigest(),
                  stock_os=args.os, transport='native emulated DIN UART', checks=[], passed=False)
    session = None
    try:
        with (args.out / 'boot.log').open('w') as log, contextlib.redirect_stdout(log):
            paths, device = prepare(str(args.firmware.resolve()), str(args.out.parent / 'emulator-cache'))
            assert device.short == 'dt1'
            os.environ.update(paths.env())
            snap = first_run(paths)
            session = Session(snap, paths.syx, hle=False)
            never_fake_set_posts(session.m, session.ev, session.profile.set_post)
        uc, midi = session.m.uc, MidiIn()

        def wait(ms=100):
            session.run_ms(ms)
            assert not session.halted, session.halted

        def tap(key):
            session.tap(session.code(key), hold_ms=40, after_ms=80)
            assert not session.halted, session.halted

        def chord(modifier, key):
            session.press(session.code(modifier)); wait(40)
            try:
                tap(key)
            finally:
                session.release(session.code(modifier))
            wait(40)

        def screen(name):
            wait(80)
            panel.write_png(session.frames[-1][1], str(args.out / (name + '.png')), scale=4)

        def sound(track=0):
            kit = int.from_bytes(uc.mem_read(symbols['FW_KIT'], 4), 'big')
            return kit + 0x20 + 162 * track

        def stored(track=0):
            return list(struct.unpack('>53h', uc.mem_read(sound(track) + 20, 106)))

        def source():
            values = stored()
            return values[17:25] + values[46:53]

        def send(data):
            midi.put(bytes(data))
            for _ in range(1000):
                if midi.delivered == midi.received: break
                if midi.deliver(session.m): session.pc = uc.reg_read(UC_M68K_REG_PC)
                wait(5)
            else:
                raise AssertionError('native UART did not drain')
            wait(100)

        wait()
        chord('FUNC', 'SRC')
        for _ in range(4): tap('DOWN')
        screen('machine-menu')
        tap('YES')
        assert uc.mem_read(sound() + 126, 1) == bytes([42])
        screen('osc-defaults')
        other = bytes(uc.mem_read(sound(1), 162))
        # Native fine stepping must take several detents to cross a range position.
        range_turns = {}
        for encoder, cc, slot in (('A',16,17), ('C',18,19), ('F',21,22)):
            send([0xb0, cc, 3])
            unchanged = stored()
            trace = [unchanged[slot]]
            for _ in range(8):
                session.turn(session.encoder(encoder), 1, after_ms=120)
                assert not session.halted, session.halted
                trace.append(stored()[slot])
            assert all(0 <= b - a < 256 for a, b in zip(trace, trace[1:])), trace
            assert 3 << 8 < trace[-1] < 5 << 8, trace
            assert (trace[2] + 128) >> 8 == 3, trace
            backwards = [trace[-1]]
            for _ in range(8):
                session.turn(session.encoder(encoder), -1, after_ms=120)
                assert not session.halted, session.halted
                backwards.append(stored()[slot])
            assert backwards[-1] < backwards[0], backwards
            for value, direction in ((0,-1), (5,1)):
                send([0xb0, cc, value])
                session.turn(session.encoder(encoder), direction * 8, after_ms=200)
                assert not session.halted, session.halted
                assert stored()[slot] == value << 8
            changed = stored(); changed[slot] = unchanged[slot]
            assert changed == unchanged, (encoder, unchanged, changed)
            assert bytes(uc.mem_read(sound(1), 162)) == other
            range_turns[encoder] = dict(forward=trace, backward=backwards)
        report['range_encoder_turns_8_8'] = range_turns
        report['checks'].append('native range encoders require multiple detents, reverse and clamp independently')

        # Lookup ABI and native fallback, independent of the UART receive path.
        for track, machine, cc, expected in ((0,42,24,164),(7,42,30,170),(0,42,16,132),
                                             (0,42,9,0),(0,3,24,0),(8,42,24,0),
                                             (16,0,24,78),(16,0,30,85)):
            context = uc.context_save()
            try:
                uc.reg_write(UC_M68K_REG_D2, 0x12345678)
                uc.reg_write(UC_M68K_REG_A2, 0x23456788)
                session.m._fast_stepper_obj.left = 200000
                actual = call(session.m, 0x4007919e, [track, machine, cc])
                assert uc.reg_read(UC_M68K_REG_PC) == 0xDEADBEE0
                assert actual == expected, (track, machine, cc, actual, expected)
                assert uc.reg_read(UC_M68K_REG_D2) == 0x12345678
                assert uc.reg_read(UC_M68K_REG_A2) == 0x23456788
            finally:
                uc.context_restore(context)
        report['checks'].append('lookup ABI, unrelated audio/MIDI/FX fallback')

        maxima = [5,5,5,127,5,5,127,5] + [127] * 7
        for value in (0,1,3,4,5,42,63,64,85,127):
            send([byte for cc in range(16,31) for byte in (0xb0,cc,value)])
            expected = [min(value, maximum) << 8 for maximum in maxima]
            assert source() == expected, (value, source(), expected)
            send([0x90, 60, 100]); wait(150)
            actual = struct.unpack('>53h', uc.mem_read(symbols['FW_VOICE_PARAMS'], 106))
            assert list(actual[:8] + actual[29:36]) == expected, (value, actual)
            send([0x80, 60, 0])
        report['checks'].append('all 15 stored/live controls and incoming range clamps through native UART')
        assert bytes(uc.mem_read(sound(1),162)) == other
        before = source(); send([0xb1,24,64]); assert source() == before
        send([0xb1,7,118]); assert stored(1)[45] == 118 << 8 and source() == before
        report['checks'].append('track isolation and stock CC fallback')

        for value in range(6):
            send([byte for cc in (16,17,18,20,21,23) for byte in (0xb0,cc,value)])
            screen('osc-position-' + str(value))
        tap('SRC'); screen('mix-values')
        for value in (0,63,64,127):
            send([byte for cc in range(24,31) for byte in (0xb0,cc,value)])
            screen('mix-position-' + str(value))
        report['checks'].append('both pages, all selector positions, switch threshold and dial/level extremes')
        # Call the image's real codec on private probe buffers, not the live kit.
        context = uc.context_save()
        try:
            source_buf, saved_buf, loaded_buf = 0x10010000, 0x10011000, 0x10012000
            for pointer in (source_buf, saved_buf, loaded_buf): session.m.ensure(pointer)
            original = bytes(uc.mem_read(sound(), 162))
            uc.mem_write(source_buf, original)
            uc.mem_write(saved_buf, bytes(160))
            uc.mem_write(loaded_buf, bytes(162))
            session.m._fast_stepper_obj.left = 2000000
            assert call(session.m, 0x4007a5a0, [saved_buf, source_buf, 0]) & 255 == 1
            assert uc.reg_read(UC_M68K_REG_PC) == 0xDEADBEE0
            assert bytes(uc.mem_read(saved_buf + 4, 4)) == bytes.fromhex('00000004')
            assert call(session.m, 0x4007a236, [loaded_buf, saved_buf]) & 255 == 1
            assert uc.reg_read(UC_M68K_REG_PC) == 0xDEADBEE0
            assert bytes(uc.mem_read(loaded_buf + 20, 106)) == original[20:126]
            assert bytes(uc.mem_read(loaded_buf + 126, 1)) == bytes([42])
            assert bytes(uc.mem_read(source_buf, 162)) == original
        finally:
            uc.context_restore(context)
        report['checks'].append('native version-4 save/load preserves all 53 slots and machine 42')
        report['screenshots'] = sorted(p.name for p in args.out.glob('*.png'))
        report['final_source_8_8'] = source()
        report['midi_bytes_delivered'] = midi.delivered
        report['passed'] = True
    finally:
        if session is not None: session.close()
        (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print('PASS:', '; '.join(report['checks']))


if __name__ == '__main__':
    main()
