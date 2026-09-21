# Signal processor program counter

Writing SP_PC restarts instruction sequencing at the written address and
discards any pending branch target. This applies when the written address is
the same as the current PC, including a delay-slot address reached by single
stepping. The register retains address bits 11:2; execution wraps within IMEM.

A PC write does not change halt, break, or single-step status. Reading the PC
or resuming execution without writing it preserves a pending branch: the delay
slot executes before the branch target.

If the delay-slot instruction itself halts, PC already names the branch target.
The pending branch bubble still occupies one RCP cycle and can elapse while
halted. It does not change PC or execute the target. After that cycle, resuming
does not repeat the bubble. Clearing HALT before another RCP cycle elapses
leaves the bubble for the next running cycle. BREAK, a write that sets HALT,
and single-step halt follow the same rule.

A PC write also discards instructions latched during an operand wait. The
register dependency history remains active, so the replacement instruction
still observes any outstanding interlock. See
[instruction timing](rsp-pipeline.md) for issue pairs and branch bubbles.

`tests/rsp/test_pc.cpp` checks same-address writes, register aliases, masked
address bits, IMEM wraparound, and writes while the processor is running. The
tests execute encoded instructions and check both the resulting PC and DMEM
stores. They also check that reading the PC and resuming from single-step halt
preserve the branch.
