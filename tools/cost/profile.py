"""Attribute the cost image's cycles with digiemu's cycle clock.

Run with mise's Python and the pinned digiemu environment, as for measure.py.
Debug information must leave the loaded image unchanged. Every call's cycles
and resulting RAM are checked against a separate, unprofiled execution.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import sys

import measure as cost


def profile_clock_type():
    from emu import cftiming

    class ProfileClock(cftiming.CycleClock):
        def __init__(self, machine):
            super().__init__(machine)
            self.category = 'setup'
            self.hits = defaultdict(Counter)
            self.extra = defaultdict(Counter)
            self.parts = {}
            self.attributed = 0
            self.on_new_block = self.decoded

        def decoded(self, block, instructions):
            parts = []
            previous, emac = -1, False
            for index, (pc, instruction) in enumerate(instructions):
                cycles = cftiming.static_cost(instruction, self.accel_max)
                if emac and instruction.op in ('from_acc', 'from_emac'):
                    cycles = max(cycles, cftiming.EMAC_STORE_EXPOSED)
                if index:
                    cycles += cftiming.stall(previous, instruction)
                parts.append((pc, cycles))
                previous = instruction.writes
                emac = bool(instruction.flags & cftiming.EMAC_OP)
            assert sum(cycles for _, cycles in parts) == block.static + block.stalls
            self.parts[block.addr] = parts

        def _resolve(self, block, next_pc):
            cycles = super()._resolve(block, next_pc)
            self.extra[self.category][block.fallthrough - block.last.length] += cycles
            self.attributed += cycles
            return cycles

        def _on_block(self, uc, address, size, data):
            before, attributed = self.cycles, self.attributed
            previous, emac = self._prev_writes, self._prev_emac
            super()._on_block(uc, address, size, data)
            block = self.blocks[address]
            edge = cftiming.stall(previous, block.first) if block.first is not None else 0
            if emac and block.first is not None and block.first.op in ('from_acc', 'from_emac'):
                edge += cftiming.EMAC_STORE_EXPOSED - 1
            own = block.static + block.stalls + edge
            assert self.cycles - before == own + self.attributed - attributed
            self.hits[self.category][address] += 1
            self.extra[self.category][address] += edge
            self.attributed += own
            assert self.attributed == self.cycles

        def instruction_costs(self, category):
            rows = self.extra[category].copy()
            for address, count in self.hits[category].items():
                for pc, cycles in self.parts[address]:
                    rows[pc] += cycles * count
            return rows

    return ProfileClock


def execute(image, symbols, blocks, clock_type):
    machine = cost.machine(image)
    clock = clock_type(machine)
    calls = []

    def call(name, scenario, phase, argument=0):
        clock.category = f'{scenario}/{phase}'
        before = clock.cycles
        cost.call(machine, symbols[name][0], argument)
        ram = bytes(machine.uc.mem_read(symbols['_ram_start'][0], symbols['_end'][0] - symbols['_ram_start'][0]))
        calls.append(dict(scenario=scenario, phase=phase, cycles=clock.cycles - before,
                          ram_sha256=hashlib.sha256(ram).hexdigest()))

    for index, scenario in enumerate(cost.SCENARIOS):
        call('cost_setup', scenario, 'setup', index)
        call('cost_block', scenario, 'first')
        for _ in range(3):
            call('cost_block', scenario, 'warmup')
        for _ in range(blocks):
            call('cost_block', scenario, 'warm')
    assert clock.decode_mismatches == 0
    return calls, clock


def source_locations(elf, addresses):
    addresses = sorted(addresses)
    result = cost.run('m68k-elf-addr2line', '-a', '-f', '-i', '-e', str(elf),
                      *(hex(pc) for pc in addresses))
    rows, current = {}, None
    for line in result.splitlines():
        if line.startswith('0x'):
            current = int(line, 16)
            rows[current] = []
        else:
            rows[current].append(line)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--blocks', type=int, default=24)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    if args.blocks < 1:
        parser.error('--blocks must be positive')
    args.out = args.out or cost.ROOT / 'out/profile'
    args.out.mkdir(parents=True, exist_ok=True)
    image, symbols, helpers = cost.build(args.out)
    # ld's default _end is not guaranteed by the existing linker script.
    spans = [symbols[name] for name in ('cost_output', 'cost_state', 'cost_params')]
    symbols['_end'] = (max(address + size for address, size in spans), 0)
    symbols['_ram_start'] = (min(address for address, _ in spans), 0)
    debug_image, _, _ = cost.build(args.out / 'debug', ['-g'])
    assert debug_image == image, 'debug information changed the executable image'
    debug = args.out / 'debug/cost.elf'
    from emu.cftiming import CycleClock
    plain, baseline = execute(image, symbols, args.blocks, CycleClock)
    calls, clock = execute(image, symbols, args.blocks, profile_clock_type())
    assert calls == plain, 'profiling changed call costs or rendered state'
    assert clock.cycles == baseline.cycles
    counters = {category: clock.instruction_costs(category) for category in clock.hits}
    assert sum(sum(c.values()) for c in counters.values()) == clock.cycles
    locations = source_locations(debug, set().union(*(set(c) for c in counters.values())))
    profiles = {}
    for category, counter in counters.items():
        rows = [dict(pc=hex(pc), cycles=cycles, source=locations[pc])
                for pc, cycles in counter.most_common() if cycles]
        profiles[category] = dict(cycles=sum(counter.values()), instructions=rows,
                                 blocks=[dict(pc=hex(pc), hits=n) for pc, n in clock.hits[category].most_common()])
    report = dict(blocks=args.blocks, helpers=helpers,
                  image_sha256=hashlib.sha256(image).hexdigest(), cycles=clock.cycles,
                  debug_image_identical=True, unprofiled_calls_identical=True,
                  decode_mismatches=clock.decode_mismatches, calls=calls, profiles=profiles)
    report['provenance'] = dict(
        revision=cost.run('git', 'rev-parse', 'HEAD').strip(),
        compiler=cost.run('m68k-elf-gcc', '--version').splitlines()[0],
        python=sys.version,
        dependency_pins=cost.run('git', 'submodule', 'status').splitlines(),
        inputs={name: hashlib.sha256((cost.ROOT / name).read_bytes()).hexdigest()
                for name in ('dsp/oscillator.c', 'dsp/oscillator.h', 'tools/cost/driver.c',
                             'tools/cost/cost.ld', 'tools/cost/measure.py', 'tools/cost/profile.py',
                             'tools/check_oscillators.py', 'tests/reference/oscillator-targets-v1.json',
                             'tests/reference/modulation-source-phases-v1.json')},
        timing_sha256=hashlib.sha256((cost.DIGIEMU / 'emu/cftiming.py').read_bytes()).hexdigest())
    (args.out / 'profile.json').write_text(json.dumps(report, indent=2) + '\n')
    for scenario in cost.SCENARIOS:
        rows = [r['cycles'] for r in calls if r['scenario'] == scenario and r['phase'] == 'warm']
        first = next(r['cycles'] for r in calls if r['scenario'] == scenario and r['phase'] == 'first')
        print(f'{scenario}: first {first}, warm mean {sum(rows) / len(rows):.1f}, worst {max(rows)}')
    print('PASS: image, every call/state, cycle attribution and decode checks')


if __name__ == '__main__':
    main()
