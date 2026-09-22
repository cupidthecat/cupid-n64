# CPU COP2 transfer latch

The VR4300 exposes a shared 64-bit COP2 transfer latch. `MTC2`, `DMTC2`, and
`CTC2` replace the latch, while `MFC2`, `DMFC2`, and `CFC2` read it. The
register index encoded by these instructions does not select separate storage.

The COP2 memory opcodes use the same latch:

- `LWC2` and `LDC2` load the aligned 64-bit doubleword containing the effective
  address.
- `SWC2` writes the low 32 bits of the latch.
- `SDC2` writes all 64 bits of the latch.

All four memory forms check the Status register's CU2 bit before accessing
memory. If CU2 is clear, they raise a coprocessor-unusable exception for COP2
without reporting an address error.

The regression suite covers an `LWC2` from the upper word of a doubleword,
subsequent `DMFC2`, both store widths, and exception priority for a disabled
coprocessor.
