"""Check the ColdFire build's hand-written assembly against its portable C under digiemu.

Builds the engine with and without assembly (MODELDIGI_NO_ASM), runs the six cost scenarios and a
set of randomized parameter sets block by block, and requires every output sample and the whole
bank state to match after every block. A copy with one arithmetic shift in the assembly made
logical must differ, so the comparison is known to detect a change. Run from the repository root:

    mise run cost:equivalence
"""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import measure  # noqa: E402

ROOT = measure.ROOT


def build(folder, defines):
    folder.mkdir(parents=True, exist_ok=True)
    engine, driver, elf = folder / 'oscillator.o', folder / 'driver.o', folder / 'cost.elf'
    measure.run('m68k-elf-gcc', *measure.CFLAGS, *defines, '-c', str(ROOT / 'dsp/oscillator.c'), '-o', str(engine))
    measure.run('m68k-elf-gcc', *measure.CFLAGS, *defines, '-c', str(ROOT / 'tools/cost/driver.c'), '-o', str(driver))
    libgcc = measure.run('m68k-elf-gcc', '-mcpu=54455', '-print-libgcc-file-name').strip()
    measure.run('m68k-elf-ld', '-T', str(ROOT / 'tools/cost/cost.ld'), str(driver), str(engine), libgcc, '-o', str(elf))
    symbols = {}
    for line in measure.run('m68k-elf-nm', '-S', str(elf)).splitlines():
        parts = line.split()
        if len(parts) == 4:
            symbols[parts[3]] = (int(parts[0], 16), int(parts[1], 16))
        elif len(parts) == 3:
            symbols[parts[2]] = (int(parts[0], 16), 0)
    image = folder / 'cost.bin'
    measure.run('m68k-elf-objcopy', '-O', 'binary', str(elf), str(image))
    return bytearray(image.read_bytes()), symbols


def trace(image, symbols, scenario_blocks, seeds, seed_blocks):
    from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_D0, UC_M68K_REG_SR
    from emu import cftiming
    from emu.harness import Machine
    m = Machine()
    m.ensure(0x40000000)
    m.uc.mem_write(0x40000400, bytes(image))
    m.uc.mem_write(measure.STOP, bytes.fromhex('4e71'))
    m.install_exceptions()
    m.uc.reg_write(UC_M68K_REG_SR, 0x2700)
    clock = cftiming.CycleClock(m)
    out_at, out_size = symbols['cost_output']
    state_at, state_size = symbols['cost_state']

    def call(name, argument=0):
        sp = measure.STACK - 8
        m.uc.mem_write(sp, struct.pack('>II', measure.STOP, argument))
        m.uc.reg_write(UC_M68K_REG_A7, sp)
        m.uc.reg_write(UC_M68K_REG_D0, 0)
        start = clock.cycles
        m.uc.emu_start(symbols[name][0], measure.STOP)
        return clock.cycles - start

    digest, blocks, cycles = hashlib.sha256(), 0, {}
    def blocks_of(count, label):
        nonlocal blocks
        total = 0
        for _ in range(count):
            total += call('cost_block')
            digest.update(bytes(m.uc.mem_read(out_at, out_size)) + bytes(m.uc.mem_read(state_at, state_size)))
            blocks += 1
        cycles[label] = total // count
    for index, name in enumerate(measure.SCENARIOS):
        call('cost_setup', index)
        blocks_of(scenario_blocks, name)
    for seed in range(1, seeds + 1):
        call('cost_random', seed)
        blocks_of(seed_blocks, 'random')
    assert clock.decode_mismatches == 0
    return digest.hexdigest(), blocks, cycles


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--scenario-blocks', type=int, default=48)
    parser.add_argument('--seeds', type=int, default=400)
    parser.add_argument('--seed-blocks', type=int, default=6)
    parser.add_argument('--out', type=Path, default=ROOT / 'out/equivalence')
    args = parser.parse_args()
    sys.path.insert(0, os.environ.get('DIGIEMU', str(ROOT / 'vendor/digiemu')))
    asm, asm_symbols = build(args.out / 'asm', [])
    c, c_symbols = build(args.out / 'c', ['-DMODELDIGI_NO_ASM'])
    assert 'modeldigi_edge_taps' in asm_symbols and 'modeldigi_edge_taps' not in c_symbols
    # Mutation: the routine's first asr.l Dx,Dy (1110 xxx0 1010 0yyy) becomes lsr.l (1110 xxx0 1010 1yyy).
    start, size = asm_symbols['modeldigi_edge_taps']
    mutated, at = bytearray(asm), start - 0x40000400
    while at < start - 0x40000400 + size and not (mutated[at] & 0xf1 == 0xe0 and mutated[at + 1] & 0xf8 == 0xa0):
        at += 2
    assert at < start - 0x40000400 + size, 'no asr.l in the routine'
    mutated[at + 1] |= 0x08
    runs = {}
    for label, image, symbols in (('asm', asm, asm_symbols), ('c', c, c_symbols), ('mutated', mutated, asm_symbols)):
        runs[label] = trace(image, symbols, args.scenario_blocks, args.seeds, args.seed_blocks)
        print(f'{label:8s} {runs[label][1]} blocks, digest {runs[label][0][:16]}')
    for name in [*measure.SCENARIOS, 'random']:
        print(f'  {name:<24} C {runs["c"][2][name]:>7}  assembly {runs["asm"][2][name]:>7} cycles a block')
    assert runs['asm'][0] == runs['c'][0], 'assembly and C differ'
    assert runs['mutated'][0] != runs['c'][0], 'the comparison missed a mutation'
    print('PASS: assembly matches C in every sample and state; the mutation is detected')


if __name__ == '__main__':
    main()
