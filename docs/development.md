# Development and verification

Use mise for tools and all project steps. A host C compiler is needed for
tests. Firmware compilation uses the same Homebrew m68k toolchain setup as
OB-1; bootstrap also installs CMake for the emulator's patched Unicorn.

```sh
mise trust
mise bootstrap
mise run test
```

Bootstrap initializes the pinned submodules, installs emulator Python
dependencies through uv with mise's Python, and builds patched Unicorn.
Host tests need neither stock firmware nor the cross-compiler, but the
Digitakt test requires the SDK submodule's headers. For host-only setup,
use `mise install` and `git submodule update --init vendor/elekloader`.

## Build steps

Place stock Digitakt mk1 OS files in the ignored `firmware/` directory.
Use the names `Digitakt_OS1.53.syx` and/or `Digitakt_OS1.54.syx`. Builds use
the SDK's hash verification and reject unknown firmware.

| Command | Result |
|---|---|
| `mise run build` | Builds Core, machine-pages and ModelDigi for every local Digitakt OS; lints the machine with its dependencies. Artifacts go into `out/os<OS>/`. |
| `mise run firmware` | Builds and patches the selected stock OS into `out/Digitakt_ModelDigi.syx`. No device transfer. |
| `mise run emu` | Builds firmware, prepares it in digiemu and opens its panel. Emulator data stays in `.digiemu/`. |
| `mise run test` | Checks silent DSP output, buffer boundaries and isolation of each Digitakt track on the host. |

The first three tasks dispatch to Digitakt and Digitone. Every Digitone
subtask is a successful placeholder with no artifact or panel. Direct
`:digitakt` and `:digitone` variants are available. `build:deps` builds
only the dependencies.

Select the stock OS with `ELEKLOADER_STOCK` in `mise.toml`; firmware paths
must follow the filename convention above. `FW_VERSION` is the firmware's
four-character version, currently `MDIG`. Builds clear old `.elemod` files
only in the matching output directories. Firmware packaging includes only
Core, machine-pages and ModelDigi. The emulator retains earlier builds and
their sessions rather than deleting them automatically.

## Evidence boundaries

Host tests verify scaffold behavior, not complete firmware execution.
SDK lint and firmware packaging verify linking and patch constraints, not
hardware operation. Opening the emulator is separate from host tests.
No physical-device qualification, performance budget, saved-sound
compatibility or Model D fidelity is claimed by this scaffold.

Stock firmware, built mods, custom firmware, emulator images and raw
research stay local and ignored. Keep dependency revisions pinned; consult
the SDK's [mod author guidance](../vendor/elekloader/AGENTS.md) before
changing integrations.
