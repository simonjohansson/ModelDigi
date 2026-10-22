# Analog character

ModelDigi's device firmware adds an analog character layer to the oscillator
bank. Host tests, the reference and alias checks, the cost tools and the
emulator build the bank without it, so the clean oscillators remain what is
compared with the Model D reference. With the layer compiled out, the engine
is bit-identical to the clean engine.

## Oscillator drift

**Source readings.** The original operation guide asks for about ten minutes
of warm-up before tuning and playing [S1, section E, p. 3]. The factory service
text treats drift faster than one semitone per minute after a key is released
as a fault, and lists oscillator-board modifications "to improve tracking and
pitch stability" [S4, sections IV and XII]. The CA3046 board lacks the later
board's temperature regulation [S5, S6]. None of these give a drift amount,
and the reference recordings were made with drift disabled, so they cannot
supply one either.

**Design choice.** Each oscillator's pitch wanders slowly and independently in
steady state, with no warm-up behaviour. The wander is a smoothed
Ornstein-Uhlenbeck process: about 2 cents RMS, with a correlation time of a
few seconds, held within ±6 cents. It is meant as a middle ground between a
precisely tuned instrument and audibly analog oscillators, not as a
measurement of a particular unit. Each instance draws its own noise, so two
ModelDigi tracks also drift apart. Drift moves an oscillator's base pitch;
modulation and CONTROL feedback act on top of it.

The wander advances on a fixed grid of 32 output frames counted from the
bank's start, so splitting a render differently never changes its output.
Drift costs a few operations per oscillator per 32 frames.

## Verification

The clean engine carries every frozen instrument, alias and modulation check.
The analog build is checked against its own declared behaviour: drift stays
within its bound, has the declared size and correlation time, wanders
independently per oscillator and per instance, sets each block's actual pitch
step to within 0.1 cent, and keeps renders split-invariant.

Sources: [S1] original operation guide; [S4] R. J. Folkman technical service
manual (contents dated 7/73); [S5] Harris CA3046 datasheet; [S6] Fairchild
microA726 datasheet. Full references are in the
[oscillator research](model-d-oscillators.md).
