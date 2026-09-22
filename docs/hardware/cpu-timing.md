# CPU timing

[Non-maskable interrupts](cpu-nmi.md) have a separate entry path that preserves
the interrupted machine state and records ErrorEPC. That document describes
the current boundary timing and the remaining console reset work.

`Cpu::step` advances the CPU clock. Connected device clocks can lag while the
CPU executes from its caches; they catch up before a device access, a scheduled
edge, a buffered store, or RSP execution. See
[RCP event scheduling](rcp-scheduling.md). Standalone instruction, register, and
memory helpers support local tests without advancing those clocks.

## Cached execution slices

`Cpu::run_slice` can retire consecutive single-cycle instructions without
returning through `Cpu::step` for every instruction. Entry is limited to cached
kseg0 execution in kernel, big-endian state with a clean pipeline. Pending NMI,
exceptions, redirects, annulment, Count-write holds, software-interrupt delays,
speculative refills, wired-register writes, buffered stores, pending host output,
and active or queued SP DMA keep the general cached path on the ordinary step
path. Loads and stores are accepted only when the aligned kseg0 access already
hits the data cache. Operations that can fault, synchronize internally, miss a
cache, or require an unsupported pipeline effect also fall back to `Cpu::step`.

For an accepted memory instruction, the current guard supplies the live cache
line and byte offset to `src/cpu/cached_memory.cpp`. Loads apply their specified
width and sign extension; stores update those cached bytes and mark the line
dirty. These accesses leave backing RDRAM and the linked-load state unchanged.
The next instruction computes its address again, including when a load changed
its own base register. Alignment faults and the external-memory doubleword load
restriction are checked before a cached access can be accepted.

The cached decoder is derived from the instruction cache. A line plan records
the line tag and all 32 instruction bytes, and all eight decoded words are rebuilt
when that image changes. The instruction at `pc` must also match the word already
latched by the preceding fetch. That comparison is repeated after device clocks
are settled and before every retired instruction. The next instruction word is
read from the live cache before it is latched. These guards preserve an older
latched word even if software or a test changes the cache line and later restores
the same byte image.

Before batching, the CPU settles deferred device time. A callback delivered by
that settle can change machine state, so the entry checks and current-word match
are evaluated again afterward. The slice stops before the next Bus event, VI line
boundary, or Count/Compare edge. Instruction and Random state and the deferred
device clocks are then advanced for exactly the instructions that retired.

When the RSP is running, a cached CPU slice can execute local RSP work at the
same CPU-to-RCP clock boundaries as ordinary stepping. A shadow of the 2:3 clock
phase determines which CPU instructions produce an RSP tick. Before each such
instruction, the next latched or fetched RSP packet must contain only local
operations; COP0 and BREAK end the slice before that tick. The CPU operation is
already checked as nonfaulting and limited to registers and cache hits. Its RSP
tick can therefore run first: both operations use disjoint state, including when
the CPU cache contains a copy of SP memory. At exit, the common device clocks
are advanced once without running the RSP a second time. DMA, single-step, and
an SP PC changed behind the pipeline prevent this path. The differential tests
cover all three clock phases, cached SP memory, and IMEM changes during a latched
operand stall. They continue ordinary stepping after each slice to check the
retained pipeline state.

The idle-loop path is narrower. It recognizes a cached self-branch with a NOP
delay slot, verifies both live instruction-cache words, and stops before the same
timer and device-visible boundaries. When the RSP is running, the idle path may
advance RSP issue, operand-stall, and branch-wait cycles within that bound. It
does so only while SP DMA is idle, single-step is off, the SP PC has not been
changed behind the pipeline, and the next RSP packet contains no COP0 operation
or BREAK. An already latched packet is checked by its latched words, so an
operand stall cannot hide a shared-register operation. If the CPU slice ends
after the branch but before its delay slot, the branch/delay-slot state is
materialized before normal stepping resumes.

## Count and Compare

Count increments once every two CPU cycles and wraps at 32 bits. MFC0 and DMFC0
sample Count after their fetch and issue cycles, including any issue interlock.
The sampled value includes cycles that have not yet reached the shared clock
update. Reading Count does not advance the clocks a second time.

An instruction that writes Count restarts the divider after the write and two
following cycles. Four consecutive independent reads immediately after that
write return the written value three times, then the written value plus one.

Reaching Compare sets Cause.IP7. The bit stays set until software writes Compare,
including when Count wraps or software writes Count again. Writing a Compare
value behind Count waits for the next wrap before matching.

The regressions in `tests/cpu/test_count_compare.cpp` exercise both divider
phases, polling loops, 32-bit wraparound, and interrupt acknowledgement. The
Count-write sequence is covered in `tests/cpu/test_timing.cpp`.

## Floating-point exception decode

A floating-point exception records Cause.CE from the following instruction's
coprocessor decode. EPC still identifies the instruction that raised the exception.
An integer instruction contributes zero; COP1 and COP2 instructions contribute
one and two. This applies to arithmetic faults and exceptions raised by a write
to FCSR.

Decode sampling uses the instruction-fetch path, including address translation
and cached instruction bytes. In a branch delay slot, the next fetch follows the
branch target. The sampled instruction does not execute, and a fault in its fetch
cannot replace the older FPU exception or change the translation fault registers.
Standalone instruction helpers have no younger decode stage and use zero for CE.

`tests/cpu/test_exception_pipeline.cpp` covers transfer and arithmetic exceptions,
branch delay slots, stale instruction-cache contents, suppressed younger faults,
and destination preservation.

## LLD alignment fault priority

For data accesses, the VR4300 gives an address error higher priority than a TLB or
XTLB miss or invalid exception. `LLD` therefore raises AdEL when its effective
address is not on an 8-byte boundary even if that address has no usable TLB
mapping. The failing load records the effective address in BadVAddr and does not
commit its destination or linked-load state.

`tests/cpu/test_translation.cpp` checks the same word-aligned but doubleword-
misaligned `LLD` with a missing mapping, an invalid mapping, and a valid mapping.
The checks also cover EPC and the branch-delay bit. See the *VR4300, VR4305,
VR4310 64-Bit Microprocessor User's Manual*, section 6.4.3, Table 6-5, and the
Address Error exception description.

## Uncached RDRAM reads

The uncached read path distinguishes RDRAM from device registers after address
translation. Reads below physical `0x03f00000` use a nominal 31-cycle memory wait.
With one instruction-issue cycle, an uncached word load takes 32 CPU cycles when
instruction fetch hits and no older write is pending. This baseline follows the
cartridge suite's VI-disabled word-load measurement. It is not a complete model
of the RDRAM bus protocol.

The CPU drains older buffered stores before issuing the read. The wait advances
Count and the connected hardware clocks before the CPU samples the returned
value. Address and translation faults occur before the memory request. Cached
accesses retain their separate hit and refill paths; device registers do not
incur the nominal RAM wait.

### Row-open wait

Each 1 MiB RDRAM bank holds one open 2 KiB row, and RI tracks which row that
is (see [RDRAM interface state](rdram-interface.md)). An uncached read whose row
is open completes in the 32 cycles above. When the bank has no open row, or a
different row is open, the read takes four more CPU cycles while RI opens the
requested row. The cartridge suite measures this as the median of 36 for an
uncached load in the bank VI is displaying from, against 32 in another bank.

The row can be closed by RI refresh, or replaced by any other RDRAM
transaction to that bank: a cache refill or writeback, a buffered store, a DMA
transfer, or the [VI line-buffer fill](video-timing.md#framebuffer-fetches-and-rdram-rows).
Cached refills keep their nominal 40- and 48-cycle waits. The cartridge suite's
cache-miss cases pass with those constants whether or not the row is open, so
the refill path does not add a separate row wait until a measurement separates
the two. Standalone memory helpers stay untimed and do not evaluate rows.

`tests/cpu/test_rdram_rows.cpp` covers all eight banks, other-bank and
chip-register requests, the cached refill path, refresh closing the row, VI
fetches in the same and another bank, the VI fetch interval, and tick-size
independence. `tests/rcp/test_ri_refresh.cpp` includes the reopened row after
a refresh stall.

Cache refills and writebacks also account for refresh that begins after the
request was issued but before its nominal response time. The transfer reaches
the horizontal boundary, RI closes the open rows, and the CPU waits through the
selected clean or dirty recovery interval before the result becomes visible. The
same clock advance continues to drive Count, Compare, and connected devices.
Recovery that is already active is converted from the request's CPU/RCP phase;
recovery that starts during the nominal transfer is appended from the phase at
the nominal response endpoint.

A single-word uncached read is not held by a refresh that begins while it is in
flight: the word completes in its nominal time, the refresh proceeds, and the
next request waits for the remaining recovery. With refresh at every horizontal
boundary, the cartridge suite measures the uncached load average at 32.54
cycles. Holding the in-flight word for the whole recovery interval put the model
at about 33.4 against that figure, and dropping the hold puts it at about 32.6.
The multi-beat refill paths keep the hold because their measured averages still
depend on it; whether that difference is real or compensates for another effect
is not settled.

`tests/cpu/test_memory_timing.cpp` checks all eight 1 MiB banks, translated
uncached addresses, clock advancement, Compare events during the wait, device
reads, cache behavior, and fault priority. `test_rdram_refresh_overlap.cpp`
checks refresh beginning inside a refill, the uncached word that completes
ahead of it, Compare advancement, and bulk-versus-single-cycle scheduler
advances.

## Cache-miss SClock synchronization

The VR4300 cache-miss sequences synchronize the internal SysAD request to
SClock after one pipeline-stall cycle and one address/start cycle. That
synchronization takes one or two PClock cycles depending on the clock phase.
The 40-cycle data-cache and 48-cycle instruction-cache refill waits remain the
nominal waits, including the one-cycle synchronization case. A refill adds one
more PClock only when the carried CPU/system-clock phase requires the second
synchronization cycle.

The phase calculation includes CPU cycles that are already pending before the
shared clock update and the two fixed PClock cycles before the synchronization
point. Count reads and device accesses complete their architectural effects
before the deferred refill's phase and refresh wait are evaluated. This includes
refresh that starts on the optional second SClock synchronization cycle.
Integer multicycle operations use the overlapping sequence described below.

This phase correction applies to data-cache refills, instruction-cache misses,
and the `CACHE FillI` operation. Uncached transfers use their own bus timing,
and cache writeback-only operations do not use the refill synchronization
sequence. The 40- and 48-cycle refill constants still contain an aggregate
memory component; this change does not derive the manual's `M` term from RDRAM
register timing.

See the *VR4300, VR4305, VR4310 64-Bit Microprocessor User's Manual*, section
10.2 and Tables 11-1 and 11-2.

## Integer execution and instruction-cache overlap

Integer multiplication and division can execute while the following instruction's
cache line is refilled. The EX-stage multicycle interlock and RF-stage
instruction-cache interlock progress together; the pipeline waits until both
finish. A `DDIV` at the end of a cached 32-byte line therefore hides a shorter
refill of the next line within its 69-cycle execution. A shorter multiply can
finish first, leaving the cache refill to determine when execution continues.

`src/cpu/multicycle.cpp` starts the waits after pending fetch and issue cycles,
once the instruction-cache interlock can be serviced.
It uses the existing refill path, including the SClock phase and any refresh
recovery, then accounts for execution cycles that remain. It does not execute
the following instruction or accept an interrupt in the middle of the multiply
or divide. Count and connected devices advance only for the elapsed processor time.

`tests/cpu/test_multicycle_fetch.cpp` checks all eight integer multiply/divide
instructions at all three CPU/system-clock phases, warm and cold next lines,
branch-target fetches from a delay slot, timer-interrupt delivery, and refresh
that either fits within or extends beyond division. Standalone instruction
helpers remain untimed.

The processor manual describes the simultaneous ICB/MCI case in section 4.7.3
(page 115), the instruction-cache interlock in section 4.6.3 (page 108), and
multicycle execution in section 4.6.4 (page 109). This implementation covers
integer multiplication and division. Older data-cache and buffered-store ordering
is preserved; overlap of those waits with execution remains outside this model.
Floating-point execution and its exception interactions still serialize with
deferred instruction refills.

## Current limits

[RI refresh](rdram-interface.md) is modeled when recovery is already active or
begins before a blocking CPU request's nominal response. The row-open wait
applies to uncached CPU reads only. Buffered stores and DMA engines change the
open row but do not yet wait for it, shared-bus arbitration between requesters
is not modeled, and per-chip RAS/minimum-interval effects, dirty-row close
time, and the RI optimize bit are not modeled.

VI's line-buffer fill updates the open row of the framebuffer bank but does not
consume bus time that other requesters would wait for. Scanout still reads the
stored image separately when a field is delivered.

The [cartridge build comparison](../testing/cartridge-build-layout.md) records
the prepared input identity and the measurements used for these timing checks,
including the same-bank uncached load. Shared arbitration remains open under
issue #4.
