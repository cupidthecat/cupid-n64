# Signal processor instruction timing

The RSP advances at RCP cycle boundaries. An issue group contains one scalar-unit
instruction, one vector arithmetic instruction, or an eligible pair containing
one of each. Vector loads, stores and COP2 register transfers use the scalar
issue unit. A pair preserves program order and cannot let its second instruction
read or overwrite a vector or vector-control register written by the first.

Scalar ALU results bypass the load interlock. Loads into scalar registers and
COP0/COP2 reads retain their destination in a two-slot dependency history; zero
never becomes a pending destination. Vector destinations remain in a three-slot
history. A dependent group waits for the affected register to become available.

| Intervening issue slots | Scalar load-result waits | Vector-result waits |
| --- | --- | --- |
| 0 | 2 cycles | 3 cycles |
| 1 | 1 cycle | 2 cycles |
| 2 | 0 cycles | 1 cycle |
| 3 | 0 cycles | 0 cycles |

An empty stall slot ages both histories. A store also waits when the second
preceding pipeline slot contains a load. COP0 and COP2 transfers participate in
both load and store scheduling. The reciprocal/move element fields and VNOP's
unused destination field have additional pairing conflicts even where they do
not name a functional operand. MTC2 and LTV retain their special VNOP conflicts.

Branches issue alone when they are first in a group. The following delay-slot
group also issues one instruction. A taken delay slot adds a bubble before the
target; a target in the upper word of an eight-byte pair starts with a single
issue. Nested branches retain the outer target as the inner branch's delay slot.

Fetched instruction words remain latched during an operand wait. An SP_PC write
discards that group, the pending branch target and any pending branch bubble,
including a write of the same address. Register dependency history and a pending
single-issue restriction remain intact. Halt/resume preserves pipeline state,
while reset clears it. Single-step mode selects one instruction.
Both members of an already selected pair execute if one writes SP_STATUS to halt
the processor.

Stalls consume normal RCP cycles. DMA and peripheral clocks continue to advance,
so an intervening SP DMA can finish while a loaded value waits to reach its
consumer. `Rsp::step()` processes one issue, dependency stall or branch bubble;
`Rsp::tick()` also advances SP DMA. `System::advance()` orders these cycles with
the other devices.

The encoded microprograms in `tests/rsp/test_pipeline*.cpp` observe PC, status,
DMEM and DPC_CLOCK. They cover dependency spacing, issue pairing and control
changes. A 4,096-iteration mixed scalar/vector loop checks elapsed clocks and
results with both bulk and single-CPU-cycle scheduling. The synchronization
regression checks DMA completion during a load
interlock and the consumer's use of the previously loaded value.

These checks establish the implemented issue model. They do not provide new
physical-console captures for every opcode combination or resolve every
same-cycle DMA/memory edge. SP transfers still use the row-based DMA model, and
shared RDRAM arbitration remains subject to the limits in
[RCP scheduling](rcp-scheduling.md).
