# Oscillator calibration, version 1

Version 1 freezes **normalized, observable REFERENCE targets**, with explicit
engineering choices where internal quantities cannot be identified. It is an
implementation basis for the CA3046-era oscillator section, not physical-unit
qualification. Raw recordings and primary-source scans remain local. The
[numeric target bundle](../tests/reference/oscillator-targets-v1.json) is portable
and contains source hashes, extraction definitions, units and acceptance limits.
It contains no oscillator renderer or alternate sound engine.

## Circuit basis and loaded mixture

Use factory Figure 9-3, the old CA3046 board, with the waveform switching in
Figure 9-17. Figure 9-2 is the later temperature-stabilized board and is not
interchangeable. See sources S2/S3 in the [architecture reference](model-d-oscillators.md).
The old-board explanation describes a decreasing ramp from approximately +4 V
to -4 V, triangle conversion and thresholded pulses; these voltages do not
define the digital output scale.

The triangle/saw switches use **10 kΩ triangle and 47 kΩ saw resistors**:
R31/R30 for oscillator 1 and R37/R36 for oscillator 2. The source level pots
R6/R7/R8 are 25 kΩ; their wipers feed 33 kΩ mixer resistors. The pulse-bias
ladder is 1.5 kΩ, 1 kΩ and 7.5 kΩ toward -10 V. Those are drawing readings,
not measurements of a particular instrument.

For effective source impedances `Zt`, `Zs` and load `Zl`, the passive node is:

```text
Gt = 1 / (10k + Zt); Gs = 1 / (47k + Zs); Gl = 1 / Zl
mix = Gt/(Gt+Gs+Gl) * triangle + Gs/(Gt+Gs+Gl) * saw
```

Even the simplified zero-source-impedance, 25 kΩ-only load gives coefficients
about 0.6201 and 0.1319, rather than 0.5 and 0.5. Actual transistor output
impedances and wiper/mixer loading matter. These simplified coefficients are
not adopted as an exact physical transfer.

An absolute-time least-squares fit of the recorded combination against its
recorded triangle and saw gives **0.6362014 × triangle + 0.1489326 × saw**.
All eight notes, MIDI 24 through 108, independently agree within 0.0000002;
residuals are below -90.7 dB. This is the adopted equivalent loaded behavioral
mixture. It does not uniquely recover transistor impedances or pot loading.

## Signed waveform and level convention

Phase zero is the decreasing-saw reset to its positive peak. Triangle starts
at its positive peak and reaches its trough at phase 0.5. Pulses are
high in the interval centered on phase zero. Reverse saw is the negative of
saw, with no independent phase fitting. Absolute physical output polarity is
not identified by this convention.

| Shape | Adopted normalized audio behavior | Basis and uncertainty |
|---|---|---|
| Triangle | Symmetric triangle component peak 0.88925678 plus 0.0168924 × saw; trough phase 0.5 | Signed harmonic-ratio inference; reset residue retained |
| Triangle/saw | The loaded combination above | Direct multi-note linear fit |
| Saw | Decreasing ramp, peak 0.94775948 | Factory direction; relative level inferred from pulse harmonics |
| Reverse saw | Negative of saw | Signed complex ratios near -1 at each harmonic |
| Square | High fraction 0.49; level difference 2 | Normalization choice plus harmonic inference |
| Wide pulse | High fraction 0.31; level difference 2 | Harmonic inference; not the often quoted 35% |
| Narrow pulse | High fraction 0.195; level difference 2 | Harmonic inference; not a universal factory threshold |

The triangle's ratios contain an approximately 0.0168924 real component at
both odd and even harmonics, plus the triangle's imaginary odd-harmonic series.
That is consistent with a small signed saw residue (a reset step about 0.03202
in this normalization). An asymmetric-knee fit can match magnitudes while
getting the even-harmonic sign wrong; it is rejected by the signed targets.
This is an equivalent decomposition, not identification of a unique transistor
leakage mechanism.

Oscillators 1/2 select triangle, combination, saw and the three pulses.
Oscillator 3 selects triangle, reverse saw, saw and the three pulses.
Audio has zero cycle mean by engineering choice; do not apply that choice to
the modulation source. Preserve relative levels and apply one common bank gain.
Bandlimited corrections and curvature must be judged against the signed target
ratios and separate alias tests, rather than assuming the approximate shapes
above are already qualified audio.

The static extractor fits constant/cosine/sine terms at a shared measured
frequency over seconds 1–2. For each harmonic it divides each shape's complex
coefficient by the corresponding saw coefficient at identical absolute time.
A common linear output response cancels in this ratio; independent phase,
gain and polarity fitting are prohibited. This cancellation is an observation-
path assumption tested across pitches, not proof of an internal tap.

MIDI 60/84 define eight harmonic ratios. MIDI 72/96/108 are holdouts, with
fewer available harmonics near Nyquist. Maximum complex holdout errors are
below 0.002; the frozen acceptance limit is 0.01. This permits core shape and
relative-level checks without baking the common audio coupling or frequency-
dependent output response into the engine. It does not measure alias energy.
Absolute low-note/LO audio shape and core DC remain excluded.

## Modulation and feedback

Use the complete-path pitch observations for control behavior. Pitch rises
during the decreasing audio-saw traversal; reverse saw reverses that motion.
The chosen control convention therefore has the opposite sign to audio.
Pulse control is **0..1**, high during the audio-low interval; its high-time
fractions are approximately 0.51, 0.69 and 0.805. Centering these pulses would
discard a real, duty-dependent pitch shift.

DSP depth is octaves multiplying this unit control source. Absolute internal
volts are unnecessary and unmeasured. Initial triangle and saw control shapes
use `-audio/2`, with triangle/saw/reverse-saw normalized biases
**+0.0261/-0.0061/-0.0035** respectively.
These are fitted behavioral approximations. The three pulses use their
unipolar convention directly. Span and both mean definitions, rather than
these approximate coefficients alone, are the acceptance targets.

The C control path uses these shapes and biases with no further gain. An
earlier refinement (triangle ×1.0010, saws ×1.0037, fitted to open-loop amounts
0.1/0.5) was removed because it compensated a measurement condition rather
than a control-path property. The frozen span estimator's fixed window holds a
non-integer number of source cycles, so a ramp's 5th–95th percentile span
depends on where the source cycle starts: by up to about 8.5 cents at amount 1.
The engine's default start phase reads ramp spans about 0.45% low. The gain
cancelled that and pushed feedback cases, which read high at that phase,
towards the 3-cent limit.

Each recording exposes its own source phase through its measured pitch trace.
Modulation acceptance therefore starts oscillator 3 at the phase that
reproduces each recording's analysed window. These phases come from the
hash-verified recordings, are kept separate from the frozen targets, and are
re-derived by the explicit local comparison. All 36 open-loop recordings imply
the same phase. With matched phases and no gain, all 48 cases pass. Worst span
errors are 0.99 cents open-loop and 0.70 cents with feedback; worst mean errors
are 0.95 and 0.74 cents. Residuals stay roughly constant in cents across a 27×
depth range, so they indicate no depth-calibration or control-amplitude scale
error. In feedback at amount 0.5, pulse source rates agree with the recordings
within 0.02%. That rate depends only on duty and depth, so oscillator 3
receives the same depth as the carrier.

Small residuals remain within limits and unexplained. The separate open-loop
saw and reverse-saw biases split the feedback means by about 1.4 cents, whereas
the recordings show equal feedback means. Feedback source rates at amount 0.1
read 0.05–0.09% low. Pulse mean frequencies carry +0.45 to +0.95 cents. Carrier
phase is not matched; it moves spans by up to about 0.2 cents. Audio-rate FM
remains unqualified. Do not refit the feedback holdouts or weaken the frozen
targets.

There are 36 new open-loop depth observations: six shapes at amounts 0.05,
0.1, 0.25, 0.5, 0.75 and 1, plus six zero-depth controls. The recorded amount
is nonlinear: square spans at 0.25/0.5/0.75/1 are about
338.48/531.50/905.56/1810.49 cents. **No continuous wheel law is frozen.**
Each case records a calibrated octave depth inferred from its matching square
control source's 1st-to-99th percentile pitch difference divided by 1200. The
bundle explicitly maps native modulation shape indices to global waveform
indices, and records the unmodulated carrier/source frequencies. The DSP
accepts actual octave depth; a later device-control mapping must state
its interpolation and validate intermediate settings. Do not assume amount
0.5 means half the maximum depth.

At amount 0.5, open-loop square/wide/narrow pulse mean-frequency shifts are
about +290.91/+383.06/+439.35 cents. Tracked feedback changes these to
+250.25/+348.22/+413.83 cents. Do not remove the mean before comparing.
The bundle also retains time-averaged cents, which is a different quantity.

There is an independent feedback check requiring no second oscillator model.
For recorded open-loop pitch displacement `p(phase)` in cents:

```text
feedback mean-frequency ratio = 1 / mean_phase(2^(-p/1200))
feedback time-mean cents = mean_phase(p * 2^(-p/1200))
                           / mean_phase(2^(-p/1200))
```

This is a cycle-time integral of recorded values. Across all six shapes at
amounts 0.1 and 0.5, both predicted feedback means agree with the independently
recorded tracked cases within 0.87 cents. This supports conditional exponential
feedback and retained offsets; it does not select a discrete integration delay
or certify audio-rate FM. Frozen span/mean tolerances are 3/2 cents respectively.

## Tuning, LO and boundaries

Normal relative tuning is approximately ±700 cents; CONTROL-off oscillator 3
widens to approximately ±3500 cents, a fivefold sweep. The adopted recorded
fits are ±700.5038 and ±3502.5 cents; these are reference calibration, not
a universal equal-temperament or factory-accuracy claim. Store measured ratios
to the middle setting, with a 1-cent acceptance limit; absolute tuning offsets
of a particular observation are not concert-pitch requirements.

CONTROL-off LO rates at tune amounts 0/0.25/0.5/0.75/1 are approximately
0.068488/0.183770/0.512146/1.401718/3.856087 Hz. These were measured through
carrier pitch movement, independently cross-checked by repetition to within
0.5%. Coupled LO triangle audio can favor harmonics or longer periods and is
excluded as the frequency oracle here. Log-frequency interpolation between
these five points is an explicit initial approximation; the table does not
identify a unique resistor transfer. Tracked LO frequency/routing and feedback
cases remain in the existing observation matrix.

All three phases advance continuously through notes and audio muting.
Oscillator 3 modulation is taken before its audio level. CONTROL off removes
incoming keyboard/bend/external/modulation control, with its dedicated tuning
path retained. No drift, bleed, sync or random detuning is added.

## Freeze and verification boundary

The bundle freezes observable targets and the labeled normalization choices.
It does not freeze unseen physical voltages, internal mixer transfer, continuous
wheel mapping, analog-unit variation, live/offline equivalence, 96 kHz behavior,
alias performance or device timing. The 414 accepted 48 kHz recordings include
67 targeted additions; originals and their prior integrity manifests remain
unchanged. New batches have their own immediate post-completion baselines.

Calibration validation covers all new formats/settings, finite audio, headroom,
stereo equality, source integrity, known-signal harmonic extraction, held-out
pitch ratios, loaded-mixture residuals, feedback cycle integrals and independent
numeric reruns. Portable DSP tests consume the committed numbers without
the WAVs. An explicit local comparison must verify the private evidence hashes
and fail clearly when recordings are missing; it must not regenerate targets.

The approved C-only interface now supports host rendering and comparison against
this bundle. Python performs measurements and static Fourier checks; it does
not implement an alternate oscillator. Device controls, saved meanings and
machine identity are later gates.
