"""Estimate the oscillator bank's ColdFire cost per 32-frame block with digiemu's cycle model.

Compiles the actual C engine with the SDK's Digitakt flags, links it with a small driver at a fixed
address and times cost_block() under emu.cftiming.CycleClock (MCF5441x manual timing tables:
zero-wait memory, so an estimate, not a device measurement). Run from the repository root:

    mise run cost [factor ...]
"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
CFLAGS = ['-mcpu=54455', '-O2', '-ffreestanding', '-fno-builtin', '-nostdlib', '-std=c99', '-Wall', '-Werror']
SCENARIOS = ['one saw', 'three sources', 'slow modulation', 'audio-rate feedback FM', 'high narrow pulses',
             'saw feedback FM']
PERIOD = 166_700        # core cycles per 32-frame block at 250 MHz
STOCK = 100_000         # stock 1.53 render estimate (digiemu docs/FIRMWARE-CHECK.md)
BUDGET = int(PERIOD * 0.9) - STOCK   # what a 10% render margin leaves for ModelDigi
STACK, STOP = 0x400ff000, 0x400ff800
HELPERS = ('__muldi3', '__divdi3', '__udivdi3', '__moddi3', '__umoddi3', '__ashldi3', '__ashrdi3', '__lshrdi3')


def run(*cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def build(factor, folder):
    folder.mkdir(parents=True, exist_ok=True)
    defines = ['-DMODELDIGI_OVERSAMPLE=%d' % factor]
    engine, driver, elf = folder / 'oscillator.o', folder / 'driver.o', folder / 'cost.elf'
    run('m68k-elf-gcc', *CFLAGS, *defines, '-c', str(ROOT / 'dsp/oscillator.c'), '-o', str(engine))
    run('m68k-elf-gcc', *CFLAGS, *defines, '-c', str(ROOT / 'tools/cost/driver.c'), '-o', str(driver))
    helpers = sorted(set(run('m68k-elf-nm', '-u', str(engine)).split()) & set(HELPERS))
    libgcc = run('m68k-elf-gcc', '-mcpu=54455', '-print-libgcc-file-name').strip()
    run('m68k-elf-ld', '-T', str(ROOT / 'tools/cost/cost.ld'), str(driver), str(engine), libgcc, '-o', str(elf))
    symbols = {}
    for line in run('m68k-elf-nm', str(elf)).splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
    image = folder / 'cost.bin'
    run('m68k-elf-objcopy', '-O', 'binary', str(elf), str(image))
    return image.read_bytes(), symbols, helpers


def measure(image, symbols, blocks):
    from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_D0, UC_M68K_REG_SR
    from emu import cftiming
    from emu.harness import Machine
    m = Machine()
    m.ensure(0x40000000)
    m.uc.mem_write(0x40000400, image)
    m.uc.mem_write(STOP, bytes.fromhex('4e71'))
    m.install_exceptions()
    m.uc.reg_write(UC_M68K_REG_SR, 0x2700)
    clock = cftiming.CycleClock(m)

    def call(name, argument=None):
        sp = STACK - 8
        m.uc.mem_write(sp, struct.pack('>II', STOP, argument or 0))
        m.uc.reg_write(UC_M68K_REG_A7, sp)
        m.uc.reg_write(UC_M68K_REG_D0, 0)
        start = clock.cycles
        m.uc.emu_start(symbols[name], STOP)
        return clock.cycles - start

    rows = []
    for index, name in enumerate(SCENARIOS):
        call('cost_setup', index)
        for _ in range(4):
            call('cost_block')
        costs = [call('cost_block') for _ in range(blocks)]
        rows.append(dict(scenario=name, mean=round(sum(costs) / len(costs)), worst=max(costs)))
    assert clock.decode_mismatches == 0, 'cycle model could not decode some instructions'
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('factors', nargs='*', type=int, default=[1, 2, 4, 8])
    parser.add_argument('--blocks', type=int, default=24)
    parser.add_argument('--out', type=Path, default=ROOT / 'out/cost')
    args = parser.parse_args()
    sys.path.insert(0, os.environ.get('DIGIEMU', str(ROOT / 'vendor/digiemu')))
    report = dict(period=PERIOD, stock=STOCK, budget_one=BUDGET, budget_each_of_two=BUDGET // 2, factors=[])
    print(f'budget per block: {BUDGET} cycles for one instance, {BUDGET // 2} each for two '
          f'(period {PERIOD}, stock {STOCK}, 10% margin)')
    for factor in args.factors:
        image, symbols, helpers = build(factor, args.out / f'f{factor}')
        rows = measure(image, symbols, args.blocks)
        report['factors'].append(dict(factor=factor, helpers=helpers, scenarios=rows))
        worst = max(r['worst'] for r in rows)
        print(f'\n{factor}x: worst {worst} cycles/block ({worst / 32:.0f}/sample), '
              f'{worst / BUDGET:.0%} of the one-instance budget; 64-bit helpers: {", ".join(helpers) or "none"}')
        for r in rows:
            print(f'  {r["scenario"]:<24} mean {r["mean"]:>8}  worst {r["worst"]:>8}')
    (args.out / 'cost.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
