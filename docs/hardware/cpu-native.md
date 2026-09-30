# Native CPU integer blocks

On x86-64 hosts, `CUPID_NATIVE_CPU` enables compilation of frequently executed
integer sequences. It defaults to `ON`. Set `-DCUPID_NATIVE_CPU=OFF`, or pass
`--no-native-cpu` to `tools/ci/validate.py`, to exercise portable CPU execution.
The RSP has a separate switch. CI includes a configuration with both disabled.

## Supported work

The compiler accepts nontrapping integer additions, subtractions, shifts,
comparisons, immediate operations, logical operations, and SYNC. Register
operations retain their 32-bit or 64-bit widths. Word results are sign extended;
SRA and SRAV shift the full source value before truncation, including when the
source is not a sign-extended word. Immediate logical operations preserve the
upper register bits. Writes to GPR zero are discarded, and reads supply zero.

The compiler also accepts aligned LB, LBU, LH, LHU, LW, LWU, and LD reads from
matching kseg0 data-cache lines. Each read checks the address, alignment, cache
validity, and tag before touching the cached bytes. LD retains the physical
address restriction used by the ordinary cached path. A failed guard restores
the block's GPR state and returns to ordinary execution. A load into GPR zero
still checks the access and discards its result. Stores and cache misses keep
their existing paths.

Compilation uses the existing cached-slice entry checks: kernel mode, big-endian
kseg0 execution, matching instruction-cache tags, and a clean pipeline. Branches,
coprocessors, HI/LO operations, overflow traps, and multicycle operations retain
their existing execution paths.

Frequently used GPRs stay in host registers within a block. The compiler loads
their initial values on first use and writes changed values back at a successful
exit. Source and destination aliases use the updated value, including when a
load replaces its own address register. No instruction in the block can deliver
a host callback or expose these temporary values to another device.

## Clock and fetch boundaries

Each compiled block contains three to seven instructions from one 32-byte
instruction-cache line. The last word of that line is left to ordinary execution
so prefetching across the next line retains its cache-miss and exception behavior.
The planner requires the entire block to fit the remaining instruction, CPU-cycle,
Count/Compare, and peripheral-event budgets.

Pending integer or FPU load interlocks and branch-delay execution prevent entry.
A running RSP with enabled RCP interrupts still advances at each CPU instruction
boundary. Native blocks update GPRs and read private data-cache bytes, which the
RSP and shared clocks do not observe. The CPU can execute a block before advancing
those RSP boundaries. If the RSP raises an interrupt before the block is complete, the CPU
restores the saved GPRs and replays only the prefix that retired before that
boundary. Compiled blocks cannot access the physical bus, deliver a callback,
or raise an exception. Already completed local RSP cycles can be consumed
together for a whole CPU block; other RSP work retains instruction boundaries.
On return, the existing slice code accounts for retired instructions,
Random, Count, and device clocks. Host callbacks see the same machine state at
the same clock as individual stepping.

## Code ownership

A line plan retains its instruction-cache tag and all 32 instruction bytes.
Changing either releases that plan's compiled entries. A backing RDRAM write
does not invalidate an unchanged instruction cache. This preserves the stale
code visible to the hardware until software invalidates or replaces the line.

A candidate compiles on its fifth eligible use. If the code generator cannot
allocate or compile a block, it stays on the portable path until the plan changes.
The 512 line plans each
hold at most seven entries; reset releases them all. Generated code receives the
current register-array pointer at entry and embeds no machine pointer.

The CPU shares the vendored SLJIT generator and write-or-execute allocator with
the RSP. Redistribution must retain `third_party/sljit/LICENSE`. C++ planning,
ownership, and scheduler code run under the sanitizer configurations. Generated
instructions are checked through comparisons with the interpreter; they do not
carry compiler-generated sanitizer instrumentation. The entry call omits only
the UBSan function-signature check required for generated entry points.

## Regression coverage

`tests/cpu/test_native_integers.cpp` compares emitted operations with the
interpreter over wide values, shift limits, immediate limits, overlapping
registers, dependency sequences, and GPR zero. It also checks rejected instruction
classes and the block-size limit.

`tests/cpu/test_native_loads.cpp` checks load widths, signed and unsigned results,
big-endian lanes, guard failures, zero destinations, address aliases, internal
and final load-use waits, and register rollback at RSP interrupt boundaries.

`tests/cpu/test_native_execution.cpp` compares complete slices with individual
stepping across short budgets, all CPU/RCP clock phases, Count/Compare edges,
instruction-cache changes, stale backing code, tag replacement, load interlocks,
branches, COP2 memory transfers, audio callbacks, interruptible local RSP work,
RSP interrupt boundaries, reset, and runtime selection. Tests
require the native instruction counter to advance on supported builds. Cartridge
validation continues to compare stepped and batched execution.
