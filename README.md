# ModelDigi

A future Minimoog Model D clone for Elektron machines. This repository is a
buildable, silent scaffold: no oscillators, filter, envelopes or controls yet.

The layout follows OB-1:

- `dsp/`: shared, firmware-independent sound engine; currently writes silence.
- `machines/digitakt/`: Digitakt mk1 registration and render callback.
- `machines/digitone/`: explicit placeholder for the future Digitone port.
- `tests/`: host checks for silence, buffer bounds and track isolation.
- `vendor/`: Elekloader SDK and digiemu, pinned to OB-1's dependency revisions.

Use mise from the repository root:

```sh
mise trust
mise bootstrap
mise run test
mise run build
mise run firmware
mise run emu
```

Supply stock Digitakt mk1 OS 1.53 or 1.54 in `firmware/`, named
`Digitakt_OS1.53.syx` or `Digitakt_OS1.54.syx`, before building. The firmware
task selects OS 1.54 by default and writes `out/Digitakt_ModelDigi.syx` with
the four-character version `MDIG`. It builds a file only.

The Digitakt scaffold registers **ModelDigi** with experimental machine ID
121 and a blank SRC page. Selecting it produces silence. Other tracks retain
their audio. In the emulator, use FUNC+SRC to choose the machine.

Unqualified build, firmware and emu tasks dispatch to both device targets.
Digitone tasks currently print a placeholder message and return success;
they produce no artifacts and open no emulator. Use `:digitakt` or
`:digitone` to select a target.

See [Architecture](docs/architecture.md), [Development](docs/development.md)
and [AGENTS.md](AGENTS.md). Code is GPL-2.0-or-later; see [LICENSE](LICENSE).
