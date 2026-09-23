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

Instruction decoding is cached as derived metadata at instruction addresses.
Each 32-byte entry stores the issued register dependencies, issue flags,
operations, and whether the instructions can execute within a local slice.
Each of the 1,024 word addresses has paired and unpaired variants, occupying
64 KiB together, plus 16 KiB of revision tags. Control-register and element-field
dependencies decide pairing during decode; they are not needed in the cached
packet once that decision is made. With tracked instruction storage, a matching
IMEM revision permits reuse without rereading the words. CPU MMIO and SP DMA
writes invalidate that revision before the next fetch. Raw storage aliases use
exact checks of both words instead. See [instruction storage](rsp-instruction-storage.md).
Once a packet has been selected, its words and execution metadata stay latched
through operand stalls until it retires or an SP_PC redirect discards it.

The active packet retains an index into the decoding table. Fetch cannot replace
an entry while a packet is latched, and copying the pipeline keeps an index into
the copy's own table. Local RSP slices prepare and check a fresh packet once.
Fresh and already-latched packets check only the instructions selected to issue.
A shared second word does not stop a slice when pairing rules exclude it, such
as after a scalar instruction or in a branch delay slot. The selected-group
check precedes a pending branch bubble; an accepted bubble ages dependencies
without latching those words before their actual fetch cycle.

A local slice reuses trusted packets across `Rsp::run_local` calls while IMEM's
revision is unchanged. When raw storage has escaped, a slice of at least eight
RCP cycles instead uses a local bitmap to record addresses already checked in
that call. Its first visit reads both words and prepares both pairing variants.
The slice stops before DMA row transfers, shared writes, and callbacks, and local
stores can write only DMEM. The bitmap is discarded when the call returns, so
raw storage is checked again even when PC has not changed.

Local slices admit COP0 reads whose values stay constant before the next device
event: SP DMA addresses, lengths, busy/full flags, and DP registers other than
DPC_CLOCK. SP_STATUS and SP_SEMAPHORE reads retain their synchronized path.
All COP0 writes and BREAK still end local execution. An idle CPU can run local
RSP work while DMA is active, but only before the next row transfer. The DMA
countdown advances by the cycles actually executed; ordinary stepping handles
the transfer cycle before issuing its RSP instruction.

Regression tests compare queued DMA rows, both transfer directions, IMEM/DMEM
wrapping, register aliases, and callback changes with ordinary stepping. Clock
fixtures use COP0 register 12 for DPC_CLOCK; register 11 reads DPC_STATUS.
DMA-polling loops also cover a clock read after a taken delay slot, including
all CPU/RSP clock phases and instruction addresses that wrap around IMEM.

`src/rsp/execution.cpp` executes the decoded scalar operation without repeating
the primary, SPECIAL, and REGIMM decoders. Link branches still test the original
source value before writing register 31, including when register 31 is the
source. COP0, COP2, and vector memory instructions retain their existing handlers.

Vector arithmetic snapshots VS and VT before writing VD, then expands the VT
element selection into eight lanes once for the instruction. This preserves VD
aliasing with either source. On compile targets with SSE2, the supported add,
subtract, carry, logic, multiply, multiply-accumulate, absolute-value, compare, clip,
merge, and accumulator-read operations use the packed path in
`src/rsp/vector_sse2.cpp`; other functions use the scalar opcode path. Vector
registers store eight numeric 16-bit lanes in host byte order. SSE2 reads and
writes those lanes with byte-safe copies. Architectural byte accesses and DMEM
transfers convert the big-endian byte order at their boundaries. Element
selection uses lane shuffles or a broadcast before either source can be
overwritten. The packed dispatcher selects the opcode once; multiplication
handlers specialize their signedness, accumulator placement, and output slice
at compile time.

The accumulator is stored as three arrays of eight 16-bit slices. Packed
multiplication and accumulation propagate carries between the low, middle, and
high arrays and wrap at 48 bits. Scalar instructions read and write those same
slices. A scalar operation that changes only the low slice preserves both upper
slices. VADD and VSUB widen their arithmetic to 32 bits, include the incoming
carry, and then saturate; their low accumulator slices retain the wrapped sum or
difference. Mixed signed/unsigned products preserve their raw 32-bit product and
its required sign extension.

VABS retains the wrapped negation in the low accumulator even when its vector
result saturates at `0x7fff`. VLT, VEQ, VNE, and VGE use the incoming VCO flags for equality
ties, write VCCL, clear VCCH and VCO, and preserve VCE. VMRG reads VCCL without
changing VCC or VCE. VSAW reads the high, middle, or low accumulator for elements
8, 9, or 10 and returns zero for the remaining element values; it does not change
the accumulator or flags.

VCH distinguishes a zero sum from a sum of minus one when producing VCO and
VCE. VCL consumes those flags and preserves the VCC bits that its selected path
does not update. VCR uses the one's complement of VT for an opposite-sign
selection. These operations update VD and the low accumulator slice while
preserving both upper slices. The clip regressions include every element and
source/destination alias, fixed flag boundaries, and VCH/VCL/VMRG instruction
chains that consume the resulting VCC.

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
Full 16-byte transfers at element zero convert all eight lanes together on
SSE2 hosts. Partial transfers retain the byte-preserving path, including at
the end of DMEM.
Packed, fractional, and transpose memory opcodes retain their lane-specific
paths. The memory tests execute every element selection and check untouched
bytes as well as the transferred result.

`tests/rsp/test_pipeline_cache.cpp` changes each fetched word independently,
exercises address collisions, copies a stalled pipeline, and checks redirects
and reset. Reused packets retain every scalar and vector dependency bit and
the load/store wait flags. It also checks fresh and latched local-execution
decisions and the ordering of shared-word checks around a branch bubble. A stalled latched packet
remains intact when IMEM changes. `test_decoded_execution.cpp` checks the source
alias on taken and untaken link branches. The vector regressions execute encoded
programs with all element selections, both sources and the destination sharing
a register, and carries through both accumulator boundaries. They check
destination lanes, flags, and all three accumulator slices through VSAW
instructions and memory stores. `test_vector_compare_sse2.cpp` adds fixed
expectations for saturation, compare ties, merges, and accumulator reads, plus a
scalar model across element selections and source aliases.

VADD and VSUB keep the wrapped 16-bit result in the accumulator's low slice
and saturate the destination only after including the incoming carry or borrow.
Both clear VCO and preserve VCC, VCE, and the upper accumulator slices. The
packed path uses signed 16-bit saturation, with an endpoint correction for
VSUB when its right operand plus borrow is 32768. Encoded tests cover every
pair of signed endpoint values with carry clear and set, including destinations
that overwrite either source.

`test_native_vector_representation.cpp` checks architectural byte order through
MFC2/MTC2, partial and packed transfers, transpose register groups, and wrapped
DMEM addresses. It observes arithmetic through stored bytes and checks VMOV's
scalar path separately from SSE2 across every element and destination lane.
`tests/cpu/test_rsp_local_window.cpp` compares idle batching with ordinary
stepping across repeated targets, pairing changes, the short-slice boundary,
wrapped IMEM pairs, stalled packets, and host edits between calls with and
without an SP_PC write.

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

`src/rsp/local_execution.cpp` groups consecutive operand bubbles, preserving
the fetched packet when a slice ends inside a stall. Branch bubbles remain
separate from instruction fetch, and DMA transfer cycles use ordinary stepping.
`tests/rsp/test_bulk_stalls.cpp` compares grouped waits with single-cycle
progression and checks machine state across partial slices.

When local RSP execution runs ahead of the shared clock, its checkpoint retains
the latched instruction words, decoded operations, and decode-cache revision.
An IMEM edit can leave an old instruction waiting in the pipeline while a later
loop iteration fetches its replacement into the same cache slot. Rewinding to
the checkpoint restores the old latched fetch; subsequent fetches still validate
against IMEM. The regression in `tests/cpu/test_cached_rsp_ordering.cpp` checks
this case across all three CPU/RCP clock phases and partial CPU slices.

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
