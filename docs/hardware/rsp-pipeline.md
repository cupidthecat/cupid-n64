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

The current issue decoder assigns no scalar dependency inputs or outputs to
reserved SPECIAL functions. Their execution fallback still computes
`rd = rs >> (rs & 31)`. An encoded load, reserved instruction, and immediate
scalar consumer preserve this behavior in the regression suite. This check
does not supply a new console timing measurement for the reserved encoding.

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

## Decoded packets and vector execution

Issue-port decoding is cached as derived metadata at instruction addresses. A
cached packet is reused only when both fetched IMEM words and the current
pairing permission match the cached entry. A change to either word, including
one made by SP DMA, is decoded again on the next packet fetch. Cache-index
collisions and wrapped instruction addresses use the same word checks. Once a
packet has been selected, its words stay latched through operand stalls until it
retires or an SP_PC redirect discards it.

The active packet retains an index into the decoding table. Fetch cannot replace
an entry while a packet is latched, and copying the pipeline keeps an index into
the copy's own table. Local CPU slices reuse the IMEM words checked for locality
when filling an empty packet. A branch-wait cycle ages the dependencies without
latching those words before their actual fetch cycle.

Vector arithmetic snapshots VS and VT before writing VD, then expands the VT
element selection into eight lanes once for the instruction. This preserves VD
aliasing with either source. On compile targets with SSE2, the supported add,
subtract, carry, logic, multiply, and multiply-accumulate operations can use the
packed path in `src/rsp/vector_sse2.cpp`; other functions use the scalar opcode
path. Packed host-vector loads and stores use byte-safe copies and convert the
vector's big-endian 16-bit lane representation explicitly. Element selection
uses lane shuffles or a broadcast before either source can be overwritten.

The accumulator is stored as three arrays of eight 16-bit slices. Packed
multiplication and accumulation propagate carries between the low, middle, and
high arrays and wrap at 48 bits. Scalar instructions read and write those same
slices. A scalar operation that changes only the low slice preserves both upper
slices. VADD and VSUB widen their arithmetic to 32 bits, include the incoming
carry, and then saturate; their low accumulator slices retain the wrapped sum or
difference. Mixed signed/unsigned products preserve their raw 32-bit product and
its required sign extension.

Reciprocal and reciprocal-square-root estimates use fixed 512-entry tables built
at compile time. Input normalization, exponent selection, negative-input
adjustments, zero and minimum-halfword results, and the shared high-input latch
are handled by the instruction path. The divider tests compare every table
entry with an independent runtime calculation and execute the high/low opcode
sequences across all table indices and special inputs.

Scalar halfword and word DMEM accesses use packed big-endian transfers when the
bytes are contiguous, with byte transfers retained at the 4 KiB wrap. Ordinary
vector loads and stores also copy contiguous spans while preserving each
opcode's vector-end truncation, modulo-16 source wrapping, and DMEM wrapping.
Packed, fractional, and transpose memory opcodes retain their lane-specific
paths. The memory tests execute every element selection and check untouched
bytes as well as the transferred result.

`tests/rsp/test_pipeline_cache.cpp` changes each fetched word independently,
exercises address collisions, copies a stalled pipeline, and checks redirects
and reset. A stalled latched packet remains intact when IMEM changes. The vector
regressions execute encoded programs with all element selections, both sources
and the destination sharing a register, and carries through both accumulator
boundaries. They check destination lanes, flags, and all three accumulator
slices through scalar VSAR instructions and memory stores.

Branches issue alone when they are first in a group. The following delay-slot
group also issues one instruction. A taken delay slot adds a bubble before the
target; a target in the upper word of an eight-byte pair starts with a single
issue. Nested branches retain the outer target as the inner branch's delay slot.

Fetched instruction words remain latched during an operand wait. An SP_PC write
discards that group, the pending branch target and any pending branch bubble,
including a write of the same address. Register dependency history and a pending
single-issue restriction remain intact. Halt/resume retains the pending pipeline
state, while reset clears it. Single-step mode selects one instruction.
Both members of an already selected pair execute if one writes SP_STATUS to halt
the processor.

If a taken delay-slot instruction halts, its pending branch bubble consumes the
next elapsed RCP cycle, including a cycle spent halted. That cycle ages the
dependency history once and advances SP DMA without issuing an instruction or
changing PC, HALT, BROKE, or the SP interrupt. Further halted cycles preserve
the remaining dependency history. Resuming with no elapsed halted cycle leaves
the bubble for the next running cycle; resuming after it has elapsed can issue
the target immediately, subject to any remaining operand interlock.

For a branch at PC `0x00`, BREAK at `0x04`, and a store at target `0x18`, the
branch runs at clock 1 and BREAK leaves PC `0x18` at clock 2. The bubble occupies
clock 3 whether HALT is cleared immediately or one halted cycle elapses first.
The target store can execute at clock 4. A longer halted interval cannot make
the same bubble run again after resume.

Stalls consume normal RCP cycles. DMA and peripheral clocks continue to advance,
so an intervening SP DMA can finish while a loaded value waits to reach its
consumer. `Rsp::step()` processes one issue, dependency stall or branch bubble;
`Rsp::tick()` advances SP DMA and can spend a pending branch bubble while halted.
`System::advance()` orders these cycles with the other devices.

The encoded microprograms in `tests/rsp/test_pipeline*.cpp` observe PC, status,
DMEM and DPC_CLOCK. They cover dependency spacing, issue pairing, reserved
SPECIAL fields, and control changes. Halt/resume fixtures check BREAK interrupts,
single-step, vector dependencies, and DMA payload completion across a pending
branch bubble. A long mixed scalar/vector loop checks elapsed clocks and results
with both bulk and single-CPU-cycle scheduling. The synchronization regression
checks DMA completion during a load interlock and the consumer's use of the
previously loaded value.

These checks establish the implemented issue model. They do not provide new
physical-console captures for every opcode combination or resolve every
same-cycle DMA/memory edge. SP transfers still use the row-based DMA model, and
shared RDRAM arbitration remains subject to the limits in
[RCP scheduling](rcp-scheduling.md).
