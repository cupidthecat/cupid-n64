# CPU timing

[Non-maskable interrupts](cpu-nmi.md) have a separate entry path that preserves
the interrupted machine state and records ErrorEPC. That document describes
the current boundary timing and the remaining console reset work.

`Cpu::step` advances the CPU and connected hardware clocks. Standalone instruction,
register, and memory helpers support local tests without advancing those clocks.

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

Blocking CPU transfers also account for refresh that begins after the request was
issued but before its nominal response time. The transfer reaches the horizontal
boundary, RI closes the open rows, and the CPU waits through the selected clean or
dirty recovery interval before the result becomes visible. The same clock advance
continues to drive Count, Compare, and connected devices. Recovery that is already
active is converted from the request's CPU/RCP phase; recovery that starts during
the nominal transfer is appended from the phase at the nominal response endpoint.

`tests/cpu/test_memory_timing.cpp` checks all eight 1 MiB banks, translated
uncached addresses, clock advancement, Compare events during the wait, device
reads, cache behavior, and fault priority. `test_rdram_refresh_overlap.cpp`
checks refresh beginning inside a transfer, Compare advancement, and
bulk-versus-single-cycle scheduler advances.

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

With the prepared extended cartridge, all 1,604 timing cases pass, including
the same-bank uncached load. The [cartridge build comparison](../testing/cartridge-build-layout.md)
records the input identity and measurements. Shared arbitration remains open
under issue #4.
