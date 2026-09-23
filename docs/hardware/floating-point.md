# Floating-point execution

`Fpu` implements COP1 register transfers, arithmetic, comparisons, conversions,
and control-register behavior. Instruction dispatch checks CU1 before decoding
the operation. Memory transfers use the CPU's normal translation and exception
paths. [CPU timing](cpu-timing.md) describes result interlocks and elapsed cycles.

## Registers and control

The register file contains 32 64-bit entries. With Status.FR set, a word transfer
uses the low half of its selected entry and preserves the high half on writes.
With FR clear, even and odd word transfers select the low and high halves of an
even entry. Doubleword transfers use the even entry in that mode.

Arithmetic source and destination mapping has separate paths from transfers.
In paired mode, the first source ignores its low register-index bit; the second
source and arithmetic destination retain their encoded indices. Word results
clear the destination's upper half. The MOV encoding copies the full selected
source and preserves the existing cause bits. These distinctions are covered
by encoded instructions in `tests/test_fpu.cpp`.

FCSR accepts writes through mask `0x0183ffff`:

| Bits | Purpose |
| --- | --- |
| 1:0 | Rounding mode: nearest/even, toward zero, toward positive infinity, toward negative infinity |
| 6:2 | Sticky inexact, underflow, overflow, division-by-zero, and invalid flags |
| 11:7 | Enables for those five exceptions |
| 16:12 | Causes for those five exceptions |
| 17 | Unimplemented-operation cause |
| 23 | Comparison condition |
| 24 | Flush-subnormal mode |

A maskable exception sets its cause. When disabled, it also sets the sticky flag;
when enabled, it raises a CPU floating-point exception and preserves the result
destination. The unimplemented-operation cause always raises that exception.
Writing FCSR can raise an exception when a cause and its enable are both set.

## Subnormals, NaNs, and conversions

Arithmetic classifies operands from their encoded bits before using host
arithmetic. Subnormal arithmetic inputs take the unimplemented path. Comparisons
accept subnormal inputs and retain their sign and ordering: the smallest positive
single-precision value, `0x00000001`, compares unequal to zero.

Subnormal arithmetic results require FCSR flush mode with the relevant exception
enables clear. Result sign and rounding direction determine whether the flushed
result is zero or the minimum normal value. NaN classification uses the VR4300
encodings; the canonical arithmetic NaNs are `0x7fbfffff` and
`0x7ff7ffffffffffff`.

Integer conversions check the input and rounded result before performing a host
integer cast. Explicit ROUND, TRUNC, CEIL, and FLOOR instructions select their own
rounding mode; CVT uses FCSR. Long conversions retain the implemented VR4300
precision boundary. Source exceptions, result exceptions, and successful
operations have separate timing tests.

## Calling-thread environment

Host arithmetic selects the guest rounding mode and isolates its exception
flags from the calling thread. FCSR controls guest flushing and exception
delivery. Every return restores the host state it changed. Register transfers
and MOV copy encoded bits without changing the host environment.

On x64, ADD, SUB, MUL, and DIV use scalar SSE instructions. The helper in
`src/fpu/binary_arithmetic.hpp` saves MXCSR, masks host traps, disables host
flush-to-zero and denormal-as-zero modes, and installs the guest rounding mode.
It reads the resulting exception flags before restoring MXCSR. The arithmetic
leaves x87 state untouched. Other targets use the portable environment helper.

Inputs and results that can raise a guest exception retain a full environment
scope. An exception fetch can deliver a host callback, so this scope also
restores any host state changed by that callback. Guest exception handling,
result normalization, register mapping, and instruction latency use the same
paths as portable arithmetic.

The helper in `src/fpu/host_environment.hpp` skips a separate rounding-mode query
after saving the full environment. It also skips setting nearest rounding when
the default environment was installed successfully. A failed default install
still attempts to set the requested rounding. If saving the full environment
fails, the fallback saves and restores the rounding mode alone.

Comparisons use integer ordering of the encoded IEEE values after checking for
NaNs. Both signed zeros compare equal; negative values reverse the unsigned
ordering of their encodings. Subnormal operands retain their magnitude. Normal
and infinite ABS/NEG inputs also use only bit operations. These paths leave the
host environment untouched. Trapping comparisons and exceptional ABS/NEG
inputs retain an environment scope because exception handling can fetch a
younger instruction and deliver a host callback while synchronizing that fetch.

This keeps an embedding application's denormal-as-zero setting from changing a
guest comparison. It also keeps enabled host division traps from terminating the
process during a guest division. The caller's rounding mode, exception flags,
trap masks, and denormal controls survive the operation.

The C++ build uses strict floating-point settings. Enabling fast-math would
invalidate the environment-sensitive arithmetic and exception checks.

`tests/cpu/test_fpu_environment.cpp` exercises comparisons with positive and
negative subnormals in both operand positions, all comparison predicates, and
x86 denormal/flush controls. It also checks guest division with host traps enabled
and host-state restoration across arithmetic, comparisons, conversions, and
guest exceptions. The x86 control-register cases compile on x86 targets; the
standard floating-point environment checks run on every supported target.
`test_fpu_host_environment.cpp` checks the helper's failure paths, integer
comparison predicates and register aliases, and restoration when an exception
fetch delivers a callback that changes the host environment.

`test_fpu_binary.cpp` compares the SSE and portable binary paths across all
rounding modes, signed zeros, finite boundaries, subnormals, and infinities.
It checks result bits, exception flags, and host-state restoration. The callback
regression also covers invalid arithmetic inputs, overflow traps, and exact
subnormal results that raise an unimplemented-operation exception.

These regressions complement the cartridge suite. Platform results and remaining
accuracy failures are recorded in [validation results](../testing/validation-results.md).
