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

## Current limits

The extended cartridge suite still detects inaccurate RDRAM read and cache-miss
timing. It also detects instruction-fetch pipeline effects in self-modifying code
and coprocessor exception attribution. Passing the default cartridge suite does
not establish cycle accuracy for these paths.
