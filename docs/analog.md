# Analog character

ModelDigi's oscillator bank includes an analog character layer: oscillator
drift and a Model D–style mixer. Firmware, emulator, host measurements and
cost tools use the same compiled engine. Reference and alias checks turn
Analog off, so the clean oscillators remain what is compared with the Model D
reference.

Each sound's Analog switch, on by default, turns the layer on or off. Off
renders the clean engine exactly and freezes the drift clock and random state.
Turning the layer back on resumes that stored drift. The mixer restarts as it
does for a new bank, so switching can click when levels are high.

The emulator and device use the same firmware image. Turn Analog off for
clean firmware comparisons instead of building a separate image. Host tests
verify that off freezes drift and that retained analog state cannot affect
the clean output.

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

The wander advances on a fixed grid of 32 Analog-on output frames counted from
the bank's start, pausing while off, so splitting a render differently never
changes its output.
Drift costs a few operations per oscillator per 32 frames.

## Mixer

**Source readings.** Each oscillator's level control (25 kΩ) feeds the mixer
bus through a 33 kΩ resistor; the external input also uses 33 kΩ and the noise
source 11 kΩ [S2, Figure 9-16]. The bus drives the filter board's audio input,
which reaches the base of one transistor of the ladder filter's bottom
differential pair through a 10 µF coupling capacitor, with about 470 Ω to an
AC-grounded bias node [S2, Figure 9-11]. A factory modification exists "to
reduce intermodulation distortion which occurs when mixing two or more
signals" [S4, section XII].

**Derivation.** A differential pair's output follows tanh(v / 2V_T), with
V_T about 26 mV. At full level each source reaches the pair attenuated by about
470 / 33,500 ≈ 0.014. The oscillator core swings about ±4 V [S3, 2-10]; how much
of that reaches the level control depends on the waveform switch's resistors,
which the drawings do not settle for every shape. A full-level saw therefore
puts roughly 20–60 mV on the pair, against a 52 mV knee: in engine units, where
a saw peaks at 0.948, the mixer is tanh(drive · s) / drive with a drive between
about 0.4 and 1.1.

**Model choice.** The drive is 0.6, inside that range. Quiet signals pass
unchanged; a single full-level saw is compressed by about 10% at its peaks,
three full-level oscillators by about 45%. The level controls set how hard the
mixer is driven, as on the instrument. Saturating a bright, already
band-limited signal at 48 kHz would fold new products back as aliasing, so the
mixer passes its input straight through and averages only the distortion over
each sample interval (antiderivative anti-aliasing, evaluated by Simpson's
rule). Off-harmonic energy for a full-level saw is about −60 dB instead of
−43 dB, and −49 dB for three full-level narrow pulses. A 5 Hz DC blocker
follows, because compressing an asymmetric pulse shifts its mean; it stands in
for the instrument's later AC coupling. The filter input's own 34 Hz coupling
(10 µF into 470 Ω) is not applied, as it would thin the lowest notes before the
Digitakt's filter.

On the Digitakt the mixer costs about 3.7% of the render period with FAST AUDIO
on.

## Verification

The clean engine carries every frozen instrument, alias and modulation check.
Analog on is checked against its own declared behaviour: drift stays
within its bound, has the declared size and correlation time, wanders
independently per oscillator and per instance, sets each block's actual pitch
step to within 0.1 cent, and keeps renders split-invariant. The mixer curve is
odd, monotone to its fixed-point resolution, unity for small signals, within
2·10⁻⁵ of full scale of tanh(drive · s) / drive and bounded; the DC blocker
settles a constant and keeps a saturated pulse at zero mean.

Sources: [S1] original operation guide; [S2] factory service drawings
(Figures 9-11 and 9-16); [S3] factory service compilation; [S4] R. J. Folkman
technical service manual (contents dated 7/73); [S5] Harris CA3046 datasheet;
[S6] Fairchild microA726 datasheet. Full references are in the
[oscillator research](model-d-oscillators.md).
