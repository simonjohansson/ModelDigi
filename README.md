![Model D oscillator controls mapped to ModelDigi’s OSC and MIX pages](docs/images/model-d-control-map.png)

Instrument photograph: [Sweetwater](https://www.sweetwater.com/store/detail/MiniModelD--moog-minimoog-model-d-analog-synthesizer-appalachian-cherry). Digitakt screens: ModelDigi.

# ModelDigi

An experimental Model D–style oscillator synth for the **Digitakt mk1**, and
a first step toward a Minimoog Model D clone. Play it from the Digitakt's
sequencer and shape its sound with the track's existing filter, amp and effects.

## What works today

- Digitakt mk1 builds target **OS 1.53 and 1.54**. Digitone support is planned.
- The current device trial plays **oscillator 1 only**, with range, waveform,
  tuning, level and sequenced notes.
- Oscillators 2 and 3 are silent, and oscillator modulation is disabled for
  this performance trial. Their controls are visible, but do not affect the
  sound yet. The image above shows the full control layout.

The three-oscillator engine is tested on a computer; enabling the full bank
on the Digitakt still needs further integration and performance checks.
ModelDigi currently supplies the oscillator section, rather than the complete
Model D filter, envelopes and amplifier.

## Playing ModelDigi

Select **ModelDigi** as a track's SRC machine. Press **SRC** to switch between
its two pages:

- **OSC:** oscillator ranges and waveforms, plus relative tuning for oscillators
  2 and 3.
- **MIX:** master tuning, oscillator levels, oscillator 3 control, and modulation
  on/off and depth.

For the current trial, use **RNG1**, **WAV1**, **TUNE** and **LEV1** to shape
oscillator 1. Other tracks keep their own sounds.

MIDI CC 16–23 control OSC; CC 24–30 control MIX in page order. Selectors use
0–5, and other values use 0–127. See Development for the companion-version
requirement before testing saved sounds.

## CPU use

**ModelDigi is a CPU-heavy mod.** It generates its waveforms in real time at
four times the audio output rate, then applies waveform corrections and
filtering to reduce unwanted digital tones (aliasing). Enabling all three
oscillators and their pitch modulation adds further work, including the
possibility of oscillator 3 modulating itself. This processing shares the
Digitakt's CPU with its other tracks and effects.

Full-bank performance and the supported number of simultaneous ModelDigi
tracks are still being qualified.

We recommend including [DigiHealth](https://github.com/irpina/digihealth#install)
when composing your firmware in Elekloader, and leaving
**SETTINGS → FAST AUDIO** ON. FAST AUDIO runs the Digitakt's existing audio
code from faster memory to free up processing time.

## Firmware and documentation

ModelDigi is included in a custom OS composed with Elekloader using your own
stock Elektron OS file. See the [build and firmware guide](docs/development.md#build-steps)
for preparation instructions.

[Development and verification](docs/development.md) covers setup, mise tasks,
the code layout and testing. [Architecture](docs/architecture.md) explains the
engine and device integration. The [oscillator research](docs/model-d-oscillators.md)
documents the Model D behavior being modeled.

Code is GPL-2.0-or-later; see [LICENSE](LICENSE).
