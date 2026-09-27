# Native RSP blocks

On x86-64 hosts, the core can compile frequently executed local RSP blocks to
native code. `CUPID_NATIVE_RSP` enables this at build time and defaults to `ON`.
Other host architectures retain the portable block executor. Set the option to
`OFF`, or pass `--no-native-rsp` to `tools/ci/validate.py`, to validate without
native code generation.

## Execution boundaries

The existing local-block planner decides instruction pairing, dependency stalls,
and the available cycle budget. A compiled block contains at most 16 instructions.
Branches, BREAK, shared COP0 accesses, single-step execution, and blocks that do
not fit the remaining budget use the existing execution paths. DMA row visibility
and the final cycle's device ordering remain controlled by the shared scheduler.

Scalar arithmetic and logical instructions operate on 32-bit values. Loads retain
big-endian DMEM byte order, sign extension, unaligned accesses, and 4 KiB wrapping.
Loads that cross the DMEM boundary use the same wrapped read helpers as portable
execution. Stores use the existing write helpers so speculative local execution
can restore every affected DMEM block when another device observes the RSP.
Vector instructions call a helper selected from the decoded opcode. Its packed
arithmetic implementation is shared with portable dispatch. Transfers,
accumulator updates, control flags, and unsupported packed operations retain the
existing C++ behavior.

Native code receives the current RSP and register-storage pointers at entry.
It contains no pointer to the machine that first compiled it. Copies of local
blocks may share immutable executable code while retaining separate machine
state and compilation bookkeeping.

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
