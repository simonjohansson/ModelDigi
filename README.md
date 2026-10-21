![Model D oscillator controls mapped to ModelDigi’s OSC and MIX pages](docs/images/model-d-control-map.png)

Instrument photograph: [Sweetwater](https://www.sweetwater.com/store/detail/MiniModelD--moog-minimoog-model-d-analog-synthesizer-appalachian-cherry). Digitakt screens: ModelDigi.

# ModelDigi

A future Minimoog Model D clone for Elektron machines. The shared C oscillator
bank runs in a host renderer against frozen reference targets. Device firmware
has the machine and its two control pages. The current Digitakt trial renders
oscillator 1 at the production 4× rate, with oscillators 2/3 silent and modulation disabled.

The layout follows OB-1:

- `dsp/`: shared, firmware-independent oscillator bank.
- `machines/digitakt/`: Digitakt mk1 registration and render callback.
- `machines/digitone/`: explicit placeholder for the future Digitone port.
- `tests/`: oscillator mechanics, frozen numeric targets and device isolation checks.
- `tools/`: actual-C WAV renderer and measurement-only comparisons.
- `vendor/`: Elekloader SDK and digiemu, pinned to OB-1's dependency revisions.

Use mise from the repository root:

```sh
mise trust
mise bootstrap
mise run test
mise run test:oscillators
mise run render out/saw.wav seconds=3 freq1=440 wave1=3
mise run build
mise run firmware
mise run emu
```

`test:modulation` checks the frozen slow-modulation cases; `test:refinement`
doubles the same C engine's internal rate. These host tasks need no private
recordings or stock firmware. See [Development](docs/development.md) for explicit
local recording comparisons and the remaining qualification limits.

Supply stock Digitakt mk1 OS 1.53 or 1.54 in `firmware/`, named
`Digitakt_OS1.53.syx` or `Digitakt_OS1.54.syx`, before building. The firmware
task selects OS 1.54 by default and writes `out/Digitakt_ModelDigi.syx` with
the four-character version `MDIG`. It builds a file only.

The Digitakt build registers **ModelDigi** as machine 42, with two SRC pages
(press SRC to flip between OSC and MIX). Oscillator 1 follows its range,
waveform, tune, level and trig note. The other oscillator and modulation values
retain their stored meanings but are disabled for this timing trial. Other
tracks retain their audio. In the emulator, use FUNC+SRC to choose the machine.

Unqualified build, firmware and emu tasks dispatch to both device targets.
Digitone tasks currently print a placeholder message and return success;
they produce no artifacts and open no emulator. Use `:digitakt` or
`:digitone` to select a target.

See [Architecture](docs/architecture.md), [Development](docs/development.md)
and [AGENTS.md](AGENTS.md). Code is GPL-2.0-or-later; see [LICENSE](LICENSE).
