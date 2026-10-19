# Digitone scaffold

Digitone support is not implemented, matching OB-1's separation. Its mise
build, firmware and emu tasks print a placeholder message and succeed without
producing artifacts or opening a panel.

The future wrapper will use the shared DSP and own its parameter mapping,
pitch, voice lifetime, firmware callbacks, output scaling and UI. No Digitone
machine ID or saved parameter layout is assigned yet.

Start from the pinned SDK's [Digitone machine example](../../vendor/elekloader/examples/dn-sine-machine)
when implementing this port.
