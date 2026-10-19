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
| `mise run test` | Checks C oscillator state, splitting, bounds and routing, plus silent device output and Digitakt track isolation. |

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

## Oscillator development

The host renderer runs the actual shared C engine. These tasks need no stock
firmware or private recordings; uv supplies pinned NumPy using mise's Python.

```sh
mise run test:oscillators
mise run render out/saw.wav seconds=3 freq1=440 wave1=3
mise run test:modulation
mise run test:refinement
```

Waveform indices are triangle, triangle/saw combination, reverse saw, saw,
square, wide pulse and narrow pulse (0–6). Renderer frequencies are resolved Hz,
levels are linear 0–1, and depth is actual octaves 0–2, not the recorded amount
control. `mod=1` enables modulation; `control=1` also feeds oscillator 3 itself.
Source 3 defaults to a slow rate and has zero audio level. All three clocks run
even at zero level. Caller-owned bank state must persist across notes.

Portable checks cover 35 signed waveform ratios, 15 resolved-frequency cases
and 56 static spectral cases: seven shapes at 375, 1500, 3000, 6000, 9000,
12000, 16000 and 20000 Hz. The spectral limit is −60 dBc over 20 Hz–20 kHz,
including errors at harmonic bins, against static Fourier identities with the
known correction/filter response. The 20–24 kHz transition band is reported
separately. This grid is not an audio-rate FM alias qualification.

The modulation task adds all 48 frozen slow-modulation cases, preserving span,
cents of mean frequency and time-averaged cents. Refinement doubles the entire
same C algorithm from 8× to 16× and checks those summaries within one tenth of
their frozen acceptance limits. It reports filter delay separately and does
not establish samplewise dynamic convergence or all-start-phase robustness.
Reports and rendered WAVs stay under ignored `out/`; targets are never rewritten.

For a local recording comparison, point explicitly at the retained evidence:

```sh
MODELDIGI_REFERENCE_DIR=/absolute/path/to/reference mise run test:reference --modulation
```

This verifies every anchored manifest, original/shared-file hash and recording
inventory before rendering. It then compares 35 matched single-source cases
using signed ratios to the same-harmonic saw, with no independent inversion,
phase or gain fit. Missing or changed evidence fails; the portable tests remain
usable without it. Complete-path bank RMS is not a core mixer target because
the recordings do not isolate mixer transfer or match free-running phases.

## Evidence boundaries

Host tests verify oscillator behavior and scaffold isolation, not complete firmware execution.
SDK lint and firmware packaging verify linking and patch constraints, not
hardware operation. Opening the emulator is separate from host tests.
Physical-device qualification, performance budget, saved-sound compatibility,
audio-rate FM quality and fidelity beyond the declared reference subset remain
outstanding. The existing emulator task still runs the silent device adapter.

Stock firmware, built mods, custom firmware, emulator images and raw
research stay local and ignored. Keep dependency revisions pinned; consult
the SDK's [mod author guidance](../vendor/elekloader/AGENTS.md) before
changing integrations.
