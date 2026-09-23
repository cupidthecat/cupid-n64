# RSP instruction storage

`RspMemory` owns the contiguous 8 KiB SP memory image: DMEM in the first half
and IMEM in the second. The public array-style accessors retain direct byte
access for tests and host code. Emulated CPU, DMA, and RSP accesses use private
storage helpers so instruction writes can invalidate decoded metadata without
making ordinary DMEM traffic pay for an instruction-memory comparison.

## Tracked writes

CPU MMIO writes and DMA transfers into IMEM advance a nonzero revision counter.
Each decoded packet records the revision at which its two words were checked.
A fresh fetch can reuse that packet when the revision and pairing mode match.
A changed revision makes it read both words again, including a packet beginning
at `0xffc` whose second word wraps to address zero. Writes to DMEM preserve the
revision. Reset and whole-storage replacement invalidate it.

Invalidation does not modify a packet already latched in the execution pipeline.
That packet retains its original words and dependencies through operand waits.
The next fresh fetch observes any intervening IMEM write. An SP_PC redirect
discards the latch through the ordinary pipeline path.

## Direct storage access

A pointer, reference, or iterator returned by a public accessor can outlive the
call. Every such access permanently switches that storage object to exact word
validation. This includes const accessors: a const pointer into an otherwise
mutable object can legally be cast back to a writable pointer. Internal accesses
and byte-wise equality do not expose storage aliases.

Reset, `fill`, and assignment preserve an object's previous alias state. They
keep its storage at the same address, so a retained alias remains usable after
reset. Copy construction creates independent storage; assignment invalidates
the destination's revision and preserves the destination's existing alias state.
Revision wrap also permanently selects exact validation.

This policy lets ordinary emulation reuse decoded packets across calls while
retaining the existing behavior of direct memory edits. The raw path checks
both IMEM words before a fresh packet is used; local slices may reuse those
checks only until they return to their caller.

`tests/rsp/test_instruction_storage.cpp` covers MMIO and DMA invalidation,
mutable and const aliases retained across reset, storage replacement, wrapped
packets, pairing changes, and writes during a latched operand wait. The broader
[pipeline regressions](rsp-pipeline.md) check instruction dependencies and timing.
