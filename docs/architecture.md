# Architecture

ModelDigi separates the sound engine from device integration, following OB-1.
The intended instrument is a Minimoog Model D clone. The oscillator bank runs in host tests and on the Digitakt machine, where
all three oscillators and their modulation sound.

| Boundary | Responsibility |
|---|---|
| Shared oscillator bank | Three continuously running phases, seven waveforms, linear source levels and conditional exponential oscillator-3 modulation. Caller-owned state; 48 kHz mono Q31 output; no firmware or UI dependencies. |
| Device audio adapter | Produce one 32-sample signed 32-bit block from the bank for each ModelDigi track. |
| Digitakt integration | Register the machine, describe its two SRC pages, decode stored values and the trig note into bank parameters, and fill only its tracks' audio blocks. |
| Digitone integration | Reserved for a future port; no implementation yet. |
| Elekloader Core and machine-pages | Firmware exports, machine registration, render dispatch and SRC page behavior. |
| Stock firmware | Sequencing and track processing around the custom source. |

The Digitakt callback inspects eight tracks and replaces only ModelDigi
blocks, leaving other tracks untouched. It uses the SDK render event. Audio callbacks
must remain bounded and must not allocate, perform I/O or wait.
Each track owns its bank state, initialized when entering ModelDigi and kept
across trigs, note changes and source muting. Muted oscillators still advance
their clocks.

Each bank's state is small: three phases, each voice's previous step and the
next samples' pending output. Rendering works through at most 32 frames at a
time in a short local buffer, without heap allocation or shared scratch, and
uses at most about 650 bytes of the audio callback's interrupted task stack,
less than the earlier oversampled engine's 776. Rendering in one call or in
pieces gives identical output and state. The private bank layout is not a
saved-sound format.

## Digitakt controls

The machine uses permanent ID 42 on SLICE's parameter bank and fills its own
voice window. Its fifteen values use exactly the slots the machine-pages
companion provides, and each page is stored where it is shown:

| Page | A | B | C | D | E | F | G | H |
|---|---|---|---|---|---|---|---|---|
| OSC | Osc 1 range | Osc 1 wave | Osc 2 range | Osc 2 frequency | Osc 2 wave | Osc 3 range | Osc 3 frequency | Osc 3 wave |
| MIX | Tune | Osc 3 control | Modulation | Mod depth | Osc 1 level | Osc 2 level | Osc 3 level | blank |

The OSC page uses MIDI CC 16–23 in the order above. The MIX page uses
CC 24–30 in order: Tune, Control, Modulation, Depth, Level 1, Level 2 and
Level 3. Incoming selectors clamp to 0–5; all other controls use 0–127.
The two switches flip at 64. Native track channels, auto channel and receive
settings still apply. A temporary local lookup hook adds the extra CCs only
for ModelDigi audio tracks; the native lookup handles other machines, MIDI
tracks, FX and unrelated controllers. A second hook supplies each control's
declared range to incoming MIDI, matching encoder limits.

Approved stored meanings:

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

The range and waveform selectors borrow the stock TUNE control's finer encoder
stepping, requiring several detents per position. Their values still span
0–5, with the existing nearest-position rounding; incoming MIDI still picks
exact positions. FRQ2/3 use the same native fine stepping. Ordinary turns pause
for three extra detents at each whole semitone; continued turns reach the values between.
FUNC+turn takes about nine detents per semitone in the turn direction,
including zero and negative offsets (128 detents across the full range).
In the wider CONTROL-off range, it takes about two detents per semitone.
Oscillator 3's wider CONTROL-off curve is unchanged;
its intermediate semitone positions use the nearest existing stored fraction
(within 0.15 cents of the displayed tuning amount). Held-step edits retain
the native lock path. Other controls retain BR's whole-unit stepping.
Frequency, tune and depth reuse the stock round visualization, without ticks.
UI-only graphics show waveform shapes, range footage, switches and level bars. Oscillator 3's second
waveform picture is reverse saw.
These pictures do not change stored values or the audio path.

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

Audio renders directly at 48 kHz. Each waveform is linear in phase between its
edges; every step (saw reset, pulse edge) and slope change (triangle corner)
is smoothed by a Kaiser-windowed sinc reaching six samples either side, so the
output is the ideal waveform low-passed by that kernel and sampled, six
samples late. The kernel's response is flat to 0.5 dB at 16 kHz and 5 dB down
at 20 kHz, and the alias checks model it exactly. Each edge's corrections are
completed to their exact sums, so the sampled output keeps the ideal
waveform's mean. The engine uses 32-bit products only and links no 64-bit
runtime helpers, so it can run inside a firmware mod. Modulation uses a
separate pre-level control tap, refreshed every sample,
preserving duty-dependent mean pitch and conditional self-feedback. Init sets
phases to 0, 1/3 and 2/3; notes, muting and selector changes do not reset them.
No second oscillator implementation serves as the sound oracle.
