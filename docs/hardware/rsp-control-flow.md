# Signal processor program counter

Writing SP_PC restarts instruction sequencing at the written address and
discards any pending branch target. This applies when the written address is
the same as the current PC, including a delay-slot address reached by single
stepping. The register retains address bits 11:2; execution wraps within IMEM.

A PC write does not change halt, break, or single-step status. Reading the PC
or resuming execution without writing it preserves a pending branch: the delay
slot executes before the branch target.

A PC write also discards instructions latched during an operand wait. The
register dependency history remains active, so the replacement instruction
still observes any outstanding interlock. See
[instruction timing](rsp-pipeline.md) for issue pairs and branch bubbles.

`tests/rsp/test_pc.cpp` checks same-address writes, register aliases, masked
address bits, IMEM wraparound, and writes while the processor is running. The
tests execute encoded instructions and check both the resulting PC and DMEM
stores. They also check that reading the PC and resuming from single-step halt
preserve the branch.
