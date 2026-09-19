# Instruction fetch ordering

The CPU fetches the following cached instruction before executing the current
instruction. A cache hit places that word in the register-fetch latch. Invalidating
the instruction cache does not discard a word already in that latch. A miss fills
the cache, but the word remains subject to an older instruction's cache operation
before it reaches the latch.

Uncached stores issue after the next instruction reaches register fetch. The CPU
can start a second following instruction fetch before sending the store's address
request. Earlier buffered stores complete before the refill. This ordering matters
when a store changes instructions near a cache-line boundary: a successful memory
write does not update instructions that the CPU has already fetched.

A branch delay slot fetches from the resolved branch target. An exception, ERET,
reset, or explicit PC change clears the fetch latch. An annulled instruction never
executes. Fetch faults in a younger instruction do not update exception registers
or stop an older instruction from completing; the fault is checked when the CPU
reaches that instruction. A failed refill leaves its cache line invalid.

The fetch request and its wait time are separate. The request orders the cache
refill before younger memory writes, while the older instruction completes before
the CPU waits for that fetch. This keeps device-register reads and Count sampling
in order at instruction-cache boundaries.

`tests/cpu/test_instruction_fetch.cpp` exercises cached instruction retention,
explicit and implicit data-cache writeback, uncached stores, consecutive code
writes, failed speculative refills, and device-clock reads across a cache miss.
The optional cartridge cycle group also covers branches and self-modifying code.

This models the fetch and memory ordering visible to these instructions. It is not
a complete simulation of every pipeline stage or external bus cycle. RDRAM timing
limits are described in [CPU timing](cpu-timing.md).
