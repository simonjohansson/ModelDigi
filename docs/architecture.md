# Architecture

ModelDigi separates the sound engine from device integration, following OB-1.
The intended instrument is a Minimoog Model D clone. The oscillator bank is
implemented for host testing; device integration remains a later stage.

| Boundary | Responsibility |
|---|---|
| Shared oscillator bank | Three continuously running phases, seven waveforms, linear source levels and conditional exponential oscillator-3 modulation. Caller-owned state; 48 kHz mono Q31 output; no firmware or UI dependencies. |
| Device audio adapter | Produce one 32-sample signed 32-bit block, currently all zeros. |
| Digitakt integration | Register the machine, select its tracks and pass their audio blocks to the shared DSP. |
| Digitone integration | Reserved for a future port; no implementation yet. |
| Elekloader Core and machine-pages | Firmware exports, machine registration, render dispatch and SRC page behavior. |
| Stock firmware | Sequencing and track processing around the custom source. |

The Digitakt callback inspects eight tracks and replaces only ModelDigi
blocks, leaving other tracks untouched. It uses the SDK render event and
contains no private firmware addresses or extra patch sites. Audio callbacks
must remain bounded and must not allocate, perform I/O or wait.

All eight SRC knobs are hidden. The scaffold uses experimental ID 121 from
the SDK's experiment range; this is not a permanent published sound identity.
No Model D parameter layout is defined. The SDK's machine-pages companion
owns sound storage; the scaffold introduces no custom storage format.

The device wrappers will own parameter decoding, note/range/tuning conversion,
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
