# Architecture

ModelDigi separates the sound engine from device integration, following OB-1.
The intended instrument is a Minimoog Model D clone. The oscillator bank is
implemented for host testing. The Digitakt machine has its controls and their
decoding; its audio stays silent until the bank is device-ready.

| Boundary | Responsibility |
|---|---|
| Shared oscillator bank | Three continuously running phases, seven waveforms, linear source levels and conditional exponential oscillator-3 modulation. Caller-owned state; 48 kHz mono Q31 output; no firmware or UI dependencies. |
| Device audio adapter | Produce one 32-sample signed 32-bit block, currently all zeros. |
| Digitakt integration | Register the machine, describe its two SRC pages, decode stored values and the trig note into bank parameters, and fill only its tracks' audio blocks. |
| Digitone integration | Reserved for a future port; no implementation yet. |
| Elekloader Core and machine-pages | Firmware exports, machine registration, render dispatch and SRC page behavior. |
| Stock firmware | Sequencing and track processing around the custom source. |

The Digitakt callback inspects eight tracks and replaces only ModelDigi
blocks, leaving other tracks untouched. It uses the SDK render event and
contains no private firmware addresses or extra patch sites. Audio callbacks
must remain bounded and must not allocate, perform I/O or wait.

## Digitakt controls

The machine uses permanent ID 10 on SLICE's parameter bank and fills its own
voice window. Its fifteen values use exactly the slots the machine-pages
companion provides, and each page is stored where it is shown:

| Page | A | B | C | D | E | F | G | H |
|---|---|---|---|---|---|---|---|---|
| OSC | Osc 1 range | Osc 1 wave | Osc 2 range | Osc 2 frequency | Osc 2 wave | Osc 3 range | Osc 3 frequency | Osc 3 wave |
| MIX | Tune | Osc 3 control | Modulation | Mod depth | Osc 1 level | Osc 2 level | Osc 3 level | blank |

The OSC page uses the bank's first eight slots, which carry MIDI CC 16–23;
the MIX page uses the companion's seven extra slots, which carry no MIDI and
are fixed at 0–127. Ranges and waveforms are six-position selectors; the two
switches flip at the midpoint. Approved stored meanings:

- Oscillator 2/3 frequency: centre is unison; steps grow quadratically to
  exact semitones at ±1, then fall on every semitone to ±7 (0.23 cents is the
  first step). Oscillator 3 with CONTROL off sweeps five times as far.
- Tune: ±100 cents, linear. Levels follow the sample level's square law;
  zero mutes without stopping oscillator 3's modulation. Depth interpolates
  the recorded amount-to-depth points, reaching 1.509 octaves.
- Pitch: 261.6256 Hz at note 60 and 8', octave ranges, and LO at the frozen
  within-mode LO/audio ratio (−7.006 octaves). With CONTROL off, oscillator 3
  ignores the note and incoming modulation, sits at C2, and at LO follows the
  frozen five-point free-running table.
- Cold defaults: one saw at 8' at full level, oscillators 2 and 3 silent,
  modulation off, CONTROL on, everything centred.

Every value borrows SLICE's BR knob, which steps in whole units and draws a
plain dial; selectors spread their six positions over it. TUNE's and LEV's
knobs step in fractions, and PLAY's graphic shows play modes.

Decoding runs once per 32-sample block from values after locks and LFOs.
Selectors round to the nearest position; continuous values use the fraction
an LFO adds. It uses 32-bit arithmetic only, because a mod links no 64-bit
runtime helpers. Sounds save as the companion's version-4 record, which only
machine-pages 1.2 or newer reads back; the experimental ID 121 was never
published and its sounds load as ONESHOT. Changing a published meaning
requires an explicit compatibility decision.

The device wrappers own parameter decoding, note/range/tuning conversion,
voice lifetime, control ramps and UI. The bank accepts resolved Q16.16 Hz,
Q16.16 octave depth and exact unsigned-16-bit linear levels. It applies a fixed
1/16 sum gain and clamps base and instantaneous frequency to 1/16–20000 Hz.
Clamped modulation trajectories are safety cases outside the fidelity domain.
Keep device contracts out of shared DSP.
Digitone must establish its own contracts when implemented.

Audio uses polynomial step/slope corrections and halfband decimation. The
current 8× internal rate is a host-quality candidate; it has not been selected
against a device CPU budget. Modulation uses a separate pre-level control tap,
preserving duty-dependent mean pitch and conditional self-feedback. Init sets
phases to 0, 1/3 and 2/3; notes, muting and selector changes do not reset them.
No second oscillator implementation serves as the sound oracle.
