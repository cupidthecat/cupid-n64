# Native RSP blocks

On x86-64 hosts, the core can compile frequently executed local RSP blocks to
native code. `CUPID_NATIVE_RSP` enables this at build time and defaults to `ON`.
Other host architectures retain the portable block executor. Set the option to
`OFF`, or pass `--no-native-rsp` to `tools/ci/validate.py`, to validate portable
RSP execution. CPU compilation has its own
[build option and execution boundaries](cpu-native.md).

## Execution boundaries

The existing local-block planner decides instruction pairing, dependency stalls,
and the available cycle budget. A compiled block contains at most 16 instructions.
A block may end with a branch issue group. Native code executes the preceding
instructions, and the ordinary branch handler retires that final group. The
delay slot remains in the next group, with its pairing restriction and taken
branch bubble. BREAK, shared COP0 accesses, single-step execution, and blocks
that do not fit the remaining budget use the existing execution paths. DMA row
visibility and the final cycle's device ordering remain controlled by the shared
scheduler.

Scalar arithmetic and logical instructions operate on 32-bit values. Loads retain
big-endian DMEM byte order, sign extension, unaligned accesses, and 4 KiB wrapping.
Loads that cross the DMEM boundary use the same wrapped read helpers as portable
execution. Stores use the existing write helpers so speculative local execution
can restore every affected DMEM block when another device observes the RSP.
Compiled vector blocks emit SSE2 instructions for VMULF, VMULU, VMUDL, VMUDM,
VMUDN, VMUDH, VMACF, VMACU, VMADL, VMADM, VMADN, VMADH, VSAR, and the six
logical operations, plus VADD, VSUB, VADDC, and VSUBC. Fractional products retain the rounding bias and positive
endpoint. Accumulating operations propagate both low- and middle-slice carries;
unsigned destinations retain their distinct saturation rules.
Register offsets and element
selections are fixed when compiling. Both operands are loaded before an aliased
destination is written. Mixed signedness, 48-bit accumulator wrapping, carry
between the middle and high slices, and signed destination saturation follow
the architectural equations. Logical operations replace only the low
accumulator slice. Products, VSAR, and logical operations preserve vector
control flags. VADD and VSUB consume the eight low carry flags, retain wrapped
low accumulator results, saturate their signed destinations, and clear both
carry groups. VADDC reports unsigned overflow; VSUBC reports unsigned borrow
and a nonzero difference in their separate carry groups. These four operations
preserve the upper accumulator slices and the comparison and extension flags.

The compiler omits a vector destination write only when a later emitted
operation overwrites it before any reader, helper, or block exit. Accumulator
updates still execute, including wrapping and carries. Aliased source reads
keep the preceding destination value live. Transfers, vector memory operations,
and unsupported vector operations form barriers for this analysis.

Other vector instructions call a helper selected from the decoded opcode and
element field. Transfers and vector memory accesses retain the existing C++
behavior. Five SIMD registers hold temporary values. Blocks with at least three
emitted vector operations and two accumulations in a helper-free chain declare
three more registers for the accumulator's slices. Those slices load lazily,
retain arithmetic results between operations, and flush before helpers and
block exit. Wrapped scalar loads flush before their conditional branch so both
the direct and helper paths see coherent accumulator memory. Cached slices are
invalidated at each helper boundary.

Carry operations receive the current machine's low and high carry pointers
through a sixth saved scalar base. Other vector blocks retain five saved bases.
Their packed mask extraction maps each architectural lane to its flag bit.

The compiler declares every used SIMD register, including registers that
Windows requires the function to preserve. Scalar-only blocks retain three
saved base registers and skip vector pointer setup. The unsigned comparison
bias is reused between emitted operations and rebuilt after a helper call.

Native code receives the current RSP and register-storage pointers at entry.
It contains no pointer to the machine that first compiled it. Copies of local
blocks may share immutable executable code while retaining separate machine
state and compilation bookkeeping.

`tests/rsp/test_native_vector_context.cpp` executes one compiled vector sequence
against independently seeded machines. It checks every element selection,
aliased destinations, all vector registers, accumulator slices, and control flags
against ordinary execution.

`tests/rsp/test_native_vector_arithmetic.cpp` compares compiled operations with
an independent scalar oracle. It checks all 16 element selections, aliased
inputs and destinations, every vector register, all accumulator slices, and
expected control flags. Repeated products cross the 32-bit saturation and
48-bit wrapping boundaries. Mixed blocks consume accumulator changes made by
ordinary VMULQ and VMACQ helpers. Overwritten-result chains retain late
consumers, all exit registers, and accumulator effects. Memory stores and COP2
transfers observe intermediate results. Fast and wrapped scalar loads retain
cached accumulator changes before a consuming helper. Add/subtract checks cover
every carry mask, all element selections, signed endpoints, unsigned overflow
and borrow, aliased operands, and carries inside cached accumulator chains. Input patterns seed nontrivial upper slices and the
48-bit sign boundary. Output stores use an explicit base register so their
signed seven-bit displacements stay within range.

`tests/rsp/test_local_blocks.cpp` compares terminal branches with individual
stepping, including taken and untaken conditions, aliased link registers,
instruction-memory wrapping, delay-slot stores, and fragmented cycle budgets.

## Code reuse and lifetime

The cache keys each instruction sequence by its complete words, decoded
operations, and length. It compiles a sequence on its fifth use. Returning
graphics or audio microcode can reuse that code after an IMEM replacement;
instruction-memory revision and pipeline-history checks still rebuild the
block's timing plan before execution.

The lookup table holds at most 4,096 sequences. Reaching that bound clears the
lookup generation. Up to 1,024 local blocks can retain additional live programs
until their entries are replaced. Reset releases both caches. A compilation
failure leaves the sequence on the portable path without changing guest state.

The vendored SLJIT code generator uses its write-or-execute allocator. Its license
is in `third_party/sljit/LICENSE` and must accompany redistributed binaries.
The C++ implementation, helper calls, and cache code remain part of sanitizer
validation. Generated instructions themselves are not compiler-instrumented;
native-versus-portable comparisons and explicit DMEM boundary tests cover those
accesses. The native entry call omits only the UBSan function-signature check,
because a generated function has no compiler-emitted signature before its entry.

## Regression coverage

`tests/rsp/test_native_blocks.cpp` checks scalar operations, register aliases,
short cycle budgets, memory wrapping, all 64 vector opcodes and 16 element
selections, IMEM replacement, exposed memory aliases, DMA boundaries, backend
changes, and reset. CPU-slice tests compare individual stepping with native
execution while CPU loads and audio callbacks observe speculative stores.
Tests require the native instruction
counter to advance on enabled hosts. That counter records host execution work,
including checkpoint replays; it is not an architectural instruction counter.

`tests/rsp/test_native_cache.cpp` checks reuse across microcode replacements,
interior instruction changes, bounded cache storage, retained-code lifetime,
independent cache copies, and concurrent execution with separate register arrays.
The complete cartridge suites also run through the ordinary stepping and batched
execution comparison. Profile packages include the code-generator sources and
headers in their source identity.
