# CPU timing

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

`tests/cpu/test_memory_timing.cpp` checks all eight 1 MiB banks, translated
uncached addresses, clock advancement, Compare events during the wait, device
reads, cache behavior, and fault priority.

## Current limits

The memory model does not yet account for refresh, row changes, or competition
between memory users. The extended cartridge suite still detects inaccurate
cache-miss timing and an uncached read sharing VI's bank. Passing the default
cartridge suite does not establish cycle accuracy for these paths.
