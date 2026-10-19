# Architecture

ModelDigi separates the sound engine from device integration, following OB-1.
The intended instrument is a Minimoog Model D clone; no part of its sound is
implemented yet.

| Boundary | Responsibility |
|---|---|
| Shared DSP | Produce one 32-sample signed 32-bit block, currently all zeros. No firmware or UI dependencies. |
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

The device wrappers will own parameter decoding, pitch conversion, voice
lifetime, output scaling and UI. Keep these contracts out of shared DSP.
Digitone must establish its own contracts when implemented.
