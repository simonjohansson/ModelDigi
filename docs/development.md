# Development and verification

Use mise for tools and all project steps. A host C compiler is needed for
tests. Firmware compilation uses the same Homebrew m68k toolchain setup as
OB-1; bootstrap also installs CMake for the emulator's patched Unicorn.
Read [AGENTS.md](../AGENTS.md) before contributing.

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

## Repository layout

The project follows OB-1's separation of sound engine and device integration:

- `dsp/`: shared, firmware-independent oscillator bank.
- `machines/digitakt/`: Digitakt mk1 registration and render callback.
- `machines/digitone/`: explicit placeholder for the future Digitone port.
- `tests/`: oscillator mechanics, frozen numeric targets and device isolation checks.
- `tools/`: actual-C WAV renderer and measurement-only comparisons.
- `vendor/`: Elekloader SDK and digiemu, pinned to reviewed revisions.

## Build steps

Place stock Digitakt mk1 OS files in the ignored `firmware/` directory.
Only OS1.54 is supported. Use the name `Digitakt_OS1.54.syx`. Builds use
the SDK's hash verification and reject unknown firmware.

| Command | Result |
|---|---|
| `mise run build` | Builds Core, machine-pages and ModelDigi for Digitakt OS1.54; lints the machine with its dependencies. Artifacts go into `out/os<OS>/`. |
| `mise run firmware` | Builds and patches the selected stock OS into `out/Digitakt_ModelDigi.syx`. No device transfer. |
| `mise run emu` | Builds firmware, prepares it in digiemu and opens its panel. Emulator data stays in `.digiemu/`. |
| `mise run test` | Checks C oscillator state, splitting, bounds and routing, plus Digitakt control decoding, value text, page layout, actual single-oscillator output, state lifetime and track isolation. |

The first three tasks dispatch to Digitakt and Digitone. Every Digitone
subtask is a successful placeholder with no artifact or panel. Direct
`:digitakt` and `:digitone` variants are available. `build:deps` builds
only the dependencies.

Select the stock OS with `ELEKLOADER_STOCK` in `mise.toml`; the default is
OS 1.54, and firmware paths must follow the filename convention above.
`FW_VERSION` is the firmware's four-character version, currently `MDIG`.
Builds clear old `.elemod` files only in the matching output directories.
Firmware packaging includes only
Core, machine-pages and ModelDigi. The emulator retains earlier builds and
their sessions rather than deleting them automatically.

The Digitakt build registers ModelDigi as machine 42. In the emulator, use
FUNC+SRC to choose it, then SRC to switch between OSC and MIX. The current
trial sounds only oscillator 1; oscillator 2/3 and modulation values retain
their stored meanings while their sound and modulation are disabled.

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
and 98 static spectral cases: seven shapes at 375, 997, 1500, 3000, 3001, 6000,
7001, 9000, 11003, 12000, 15997, 16000, 19001 and 20000 Hz. Six frequencies
do not divide 48 kHz, reducing coincident harmonic/alias cancellation.
The approved range remains 1/16–20000 Hz. The spectral limit over 20 Hz–20 kHz
is −60 dBc for fundamentals up to 12 kHz and −50 dBc above, where fundamentals
are musically rare and the production rate's narrow pulse leaves about
−56 dBc. The residual includes errors at harmonic bins and is measured
against static Fourier identities with the known correction/filter response. The 20–24 kHz transition band is reported
separately. This grid is not an audio-rate FM alias qualification.

GitHub Actions runs the host mechanics, portable spectral/measurement checks
and all frozen slow-modulation cases through mise. It needs neither private
recordings nor stock firmware. Host checks run at the production 4× rate.

Cost reductions may change output when every frozen instrument tolerance and
the spectral limit still pass. The complete-firmware render keeps a 10%
reserve in its worst tested case.

`mise run cost [factor ...]` estimates the ColdFire cost of one bank per
32-frame block. It compiles the actual engine with the SDK's Digitakt flags,
links it with a small driver at a fixed address, and times six fixed
scenarios under digiemu's cycle model, which uses the processor manual's
timing tables and assumes zero-wait memory. It also reports any 64-bit
runtime helper the engine would import. The historical stock-render estimate is
about 100,000 of the 166,700 cycles in each period at 250 MHz, so a 10%
render margin leaves about 50,000 cycles for one instance or 25,000 for each
of two. These are estimates; the firmware check's timing stage and the device
remain the authority. The task needs the cross-toolchain and a bootstrapped
digiemu.

`mise run cost:profile --blocks 24` profiles the same six scenarios at 4×,
or at the rate given by `--factor`, separating setup, the first block, warm-up
and warm rendering. It attributes
cycles to basic blocks and source instructions, including inlined helpers.
Its checks require debug information to preserve the executable image and
profiled execution to match every unprofiled call's cycles and output/state.
The attributed total must equal the cycle clock, with no decoder mismatches.
The report and debug images stay under ignored `out/profile-<factor>x/`.
These are isolated engine estimates, not complete-firmware or physical-device timing.

On hardware, time engine calls separately from the complete firmware render.
The change from a zero-output baseline can include native processing and
cache effects, so it is not an isolated engine cost. Calibrate timer overhead
with an empty call and match call timestamps to the same native measurement
windows. To test signal-dependent work, hold code, addresses and executed copy
instructions constant and change only the delivered sample values; time the
callback separately. Discarded output is an attribution diagnostic. Capacity
acceptance still uses audible output and the complete render's worst-case
reserve.

The modulation task adds all 48 frozen slow-modulation cases, preserving span,
cents of mean frequency and time-averaged cents. Each case renders its
recording's duration, analyses the same window, and starts oscillator 3 at the
phase observed in that recording. That phase aligns the first two modulation
periods only. The same lag over the last two periods is recorded as timing
drift, carried into each modulation result and summarized in the output; it is
reported, not corrected, and is not an acceptance target. The phase table is
committed beside the frozen targets, not inside them. Feedback phases depend on the engine's
behavior during the one-second lead-in, and portable checks cannot detect a
stale table; after any change to the modulation path, run the local recording
comparison below with `--modulation`. Refinement doubles the entire
same C algorithm from 4× to 8× and checks those summaries within one tenth of
their frozen acceptance limits. It reports filter delay separately and does
not establish samplewise dynamic convergence or all-start-phase robustness.
Reports and rendered WAVs stay under ignored `out/`; targets are never rewritten.

For start-phase diagnostics, the host renderer accepts `phase1`, `phase2` and
`phase3` in cycles, from 0 inclusive to 1 exclusive. Omitted values retain the
engine's initialization exactly; overrides affect only the host tool's initial
scratch state and add no device or shared-engine control.

`mise run test:phase-sweep` probes reverse-saw feedback at amount 0.5 over four
carrier phases × eight source phases, plus the canonical start. It applies the
unchanged frozen span and both mean limits, and exits unsuccessfully if any
probe fails. It is an explicit diagnostic of start-phase sensitivity, not part
of the default CI suite. Use `--renderer`, `--factor` and a separate `--out` to
repeat with a higher-rate build of the same C engine.

Finite-window pitch-span percentiles can depend on the source phase even in a
steady signal: at amount 1, open-loop ramp spans move by about 8.5 cents across
start phases. That is why acceptance uses each recording's observed phase.
Keep this estimator sensitivity separate from reference error and
rate-refinement error. A complete-period span is useful diagnostically, but
must not replace the frozen extractor or its targets without review.

The phase report also includes a separate cycle diagnostic for this descending
slow-ramp case. Observed mid-pitch crossings delimit complete modulation cycles;
time-weighted percentiles of the linearly interpolated measured frequency avoid
both a fitted repetition period and a second 1 kHz resampling grid. Marker
times, period range and pitch quantiles are reported for inspection. This is
not a generic audio-rate pitch estimator. The original frozen-window result
still determines every `passed` flag and the command's exit status.

Carrier-period pitch measurements average across each carrier cycle, especially
around abrupt modulation resets. Distinguish that measurement aperture from
the actual C phase-advance rate. The host state regression verifies that a
carrier starting-phase change leaves its pitch increments and oscillator-3
feedback state identical; it does not establish physical-unit fidelity.

For a local recording comparison, point explicitly at the retained evidence:

```sh
MODELDIGI_REFERENCE_DIR=/absolute/path/to/reference mise run test:reference --modulation
```

This verifies every anchored manifest, original/shared-file hash and recording
inventory before rendering. It then compares 35 matched single-source cases
using signed ratios to the same-harmonic saw, with no independent inversion,
phase or gain fit. With `--modulation` it also re-derives every oscillator-3
start phase from the recordings, and fails if any differs from the committed
table by more than 0.004 cycles. The derived table is written to the report
directory and never replaces the committed one automatically. Missing or
changed evidence fails; the portable tests remain usable without it. Complete-path bank RMS is not a core mixer target because
the recordings do not isolate mixer transfer or match free-running phases.

## Retained device recordings

Analyse a retained oscillator 1 USB capture without connecting to hardware:

```sh
mise run analyse:device:oscillator /absolute/path/to/capture --out out/device-analysis
```

The capture directory contains stereo 48 kHz PCM24 audio and its capture log.
Analysis rejects changed audio, missing frames, reported input overflows,
failed restoration and invalid measurement windows. Native processed values
must be decoded through the actual C control adapter during capture; Python
measures audio and does not supply another sound engine. Retain the exact
firmware, companion revisions, native settings and event/probe frame indices
beside the recordings. Capture control and restoration are deliberately separate
from this offline command.

Waveform batches contain all six oscillator 1 shapes at each pitch and repeated
saw windows in one continuous recording. One frequency and absolute sample
positions preserve their shared phase origin. Signed ratios to the same
harmonic of the saw cancel a stable linear output path; they do not identify
absolute internal voltage, polarity or nonlinear mixer transfer. The command
fails when these ratios exceed the existing frozen complex-ratio tolerance.
Add `--reference-dir /absolute/path/to/reference` to verify every anchored
manifest and retained recording hash before comparing to the frozen bundle.
No independent waveform phase, polarity or gain adjustment is applied.

Pitch, level, native LFO and note/mute continuity modes report measurements;
they do not provide an overall device qualification flag. Separate accepted
native notes from unsupported input routes, and distinguish nominal USB sample
clock accuracy from an external pitch calibration. Level comparisons use
fundamental amplitude relative to a static full-level take, with enough
harmonics and carrier cycles to avoid finite-window leakage. Native LFO probes
have a finite read aperture and USB input latency; retain those uncertainties
instead of fitting a time offset. Carrier-frequency percentiles must be time
weighted, since high pitches otherwise contribute more crossings. A waveform
or LFO capture does not establish sequenced parameter-lock recording/playback,
full-bank modulation, audio-rate alias performance or two-instance cost.

## Evidence boundaries

For compiled Digitakt control checks, supply an already packaged image:

```sh
mise run test:firmware:controls --firmware out/Digitakt_ModelDigi.syx --os 1.54 --out out/control-check
```

The output directory must be new. This check boots native firmware, exercises
CC 16–30 through the emulated DIN receiver, checks stored and live values,
verifies that frequency edits reach the working saved record, tests unrelated
lookup fallbacks and captures both pages across selector and
switch positions. It preserves the emulator's MIDI semaphore guard and paces
input to its UART model; it does not establish burst throughput or USB MIDI
behavior. The check neither builds firmware nor transfers it to a device.

The pinned machine-pages 1.2.1 keeps in-place parameter edits packed in the
existing version-4 sound record. Ordinary builds include this fix; no local
companion replacement is needed. Control acceptance must compare the actual
working saved record with the live sound, in addition to a fresh codec
round-trip. Existing damaged records require a complete re-save after their
values have been restored. Keep the exact dependency, image and readback
hashes with persistence evidence.

Host tests verify oscillator behavior and adapter isolation, not complete firmware execution.
SDK lint and firmware packaging verify linking and patch constraints, not
hardware operation. Opening the emulator is separate from host tests.
Physical acceptance remains scoped to the retained device recordings. Complete
performance budget, broader saved-sound compatibility, audio-rate FM
quality and fidelity beyond the declared reference subset remain outstanding.
The emulator task runs the restricted one-oscillator timing trial
at 4×; it does not enable the full bank or modulation.

Stock firmware, built mods, custom firmware, emulator images and raw
research stay local and ignored. Keep dependency revisions pinned; consult
the SDK's [mod author guidance](../vendor/elekloader/AGENTS.md) before
changing integrations.
