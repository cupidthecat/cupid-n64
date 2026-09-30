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
still checks the access and discards its result. Cache misses keep their existing
paths.

Aligned SB, SH, SW, and SD writes use the same live kseg0 cache checks. Generated
code stages each destination and value; it commits the writes in instruction
order only after every address guard passes. Overlapping stores preserve byte
order and mark the line dirty. Loads after a store end the native block so the
ordinary cached path reads the committed bytes. Stores remain private to the
CPU cache until the ordinary writeback path transfers them to backing memory.

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
boundary. Native blocks update GPRs and private data-cache bytes, which the
RSP and shared clocks do not observe. The CPU can execute a block before advancing
those RSP boundaries. If the RSP raises an interrupt before the block is complete,
the CPU restores the saved GPRs, reverses cache stores and dirty flags, and replays
only the prefix that retired before that boundary. Compiled blocks cannot access
the physical bus, deliver a callback, or raise an exception. Already completed
local RSP cycles can be consumed
together for a whole CPU block; other RSP work retains instruction boundaries.
On return, the existing slice code accounts for retired instructions,
Random, Count, and device clocks. Host callbacks see the same machine state at
the same clock as individual stepping.

## Code ownership

A line plan retains its instruction-cache tag and all 32 instruction bytes.
Changing either releases that plan's compiled entries. A backing RDRAM write
does not invalidate an unchanged instruction cache. This preserves the stale
code visible to the hardware until software invalidates or replaces the line.

A candidate requests code on its fifth eligible use. A CPU-owned lookup cache
reuses immutable programs with exactly the same instruction words and length.
Hardware cache tags and bytes still decide which line plan can execute; lookup
reuse changes no fetch, warmup, access guard, or clock boundary.

The lookup holds at most 4,096 entries, including failed compilation attempts.
When it fills, the next new sequence clears the lookup generation. The 512 line
plans can retain at most seven code owners each, so clearing the lookup does not
invalidate a program in use. A failed compilation keeps ordinary execution until
a new lookup generation or reset permits another attempt. Reset releases both
lookup and line-plan ownership. Copies start with independent lookup storage;
existing line-plan copies can share immutable programs. Portable builds allocate
no lookup storage.

Generated code receives the current register-array and data-cache pointers at
entry and embeds no machine pointer or program address. Identical sequences can
therefore reuse code after instruction-cache replacement or at another address.

The CPU shares the vendored SLJIT generator and write-or-execute allocator with
the RSP. Redistribution must retain `third_party/sljit/LICENSE`. C++ planning,
ownership, and scheduler code run under the sanitizer configurations. Generated
instructions are checked through comparisons with the interpreter; they do not
carry compiler-generated sanitizer instrumentation. The entry call omits only
the UBSan function-signature check required for generated entry points.

## Regression coverage

`tests/cpu/test_native_cache.cpp` checks complete instruction keys, lengths,
changed bytes, failed compilation reuse, bounded generations, retained programs
after eviction, reset, copy and move ownership, and portable allocation behavior.

`tests/cpu/test_native_integers.cpp` compares emitted operations with the
interpreter over wide values, shift limits, immediate limits, overlapping
registers, dependency sequences, and GPR zero. It also checks rejected instruction
classes and the block-size limit.

`tests/cpu/test_native_loads.cpp` checks load widths, signed and unsigned results,
big-endian lanes, guard failures, zero destinations, address aliases, internal
and final load-use waits, and register rollback at RSP interrupt boundaries.

`tests/cpu/test_native_stores.cpp` checks store widths, overlapping writes, zero
sources, address aliases, failed guards after staged writes, reverse rollback,
following loads, partial budgets, clock phases, Count/Compare and RSP interrupt
boundaries, and DMA visibility before cache writeback.

`tests/cpu/test_native_execution.cpp` compares complete slices with individual
stepping across short budgets, all CPU/RCP clock phases, Count/Compare edges,
instruction-cache changes, stale backing code, tag replacement, load interlocks,
branches, COP2 memory transfers, audio callbacks, interruptible local RSP work,
RSP interrupt boundaries, reset, and runtime selection. Tests
require the native instruction counter to advance on supported builds. Cartridge
validation continues to compare stepped and batched execution.
