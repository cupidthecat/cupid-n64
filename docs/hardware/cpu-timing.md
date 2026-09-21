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

## Current limits

[RI refresh](rdram-interface.md) is modeled when recovery is already active or
begins before a blocking CPU request's nominal response. Row-change delays and
shared-memory arbitration remain incomplete. Buffered stores and DMA engines do
not yet share this transaction timing, and per-chip RAS/minimum-interval effects
and the RI optimize bit are not modeled.

VI advances scanline timing and starts RI refresh, but does not submit timed
framebuffer reads to RDRAM. Scanout reads the stored image separately. The CPU
wait calculation therefore has no VI request address or bank to compare with
its own transfer. Refresh alone cannot distinguish a read that shares VI's
bank from one in an adjacent bank.

At revision `6d8f70b`, the retained hosted extended image passes all eight
VI-disabled cache-miss cases. The two VI-enabled averages remain below their
original tolerance, and the uncached read sharing VI's bank still fails. The
[cartridge build comparison](../testing/cartridge-build-layout.md) records
the input identity and measurements. Shared arbitration remains open under
issues #4, #5, and #6.
