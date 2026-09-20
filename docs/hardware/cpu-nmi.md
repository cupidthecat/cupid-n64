# CPU non-maskable interrupts

`Cpu::request_nmi()` latches an NMI event for the next `Cpu::step` boundary.
Requests made before that boundary coalesce into one pending event. Taking the
event clears the latch; another request can interrupt the handler even while
ERL is set. A request does not immediately change registers or advance clocks.
Untimed instruction helpers do not service the pending event.

## Entry and return state

NMI takes priority over ordinary interrupt delivery, instruction fetch, and the
core's frozen-bus check. IE, EXL, ERL, interrupt masks, and privilege mode do not
mask it. Entry sets Status.ERL, SR, and BEV, clears TS, and redirects to
`0xffffffffbfc00000`. Other Status bits remain intact.

ErrorEPC receives the interrupted PC. When a branch delay slot is pending, it
receives the branch address so an error return can restart the branch and its
slot. An annulled branch-likely slot has already been skipped and does not move
the saved address backward. NMI preserves EPC and Cause, including Cause.BD;
ErrorEPC has no separate delay-slot flag. These rules follow the processor
manual's ErrorEPC description, NMI exception description, and reset/NMI flowchart
([NEC VR4300 manual](https://hack64.net/docs/VR43XX.pdf), pages 179, 185, and 205).

General registers, HI/LO, floating-point state, CP0 state outside the entry
changes, TLB entries, and cache contents remain available. The load-link state
survives entry; ERET clears it. ERET uses ErrorEPC while ERL is set and clears
ERL while preserving EXL and EPC. The prefetched instruction and pending branch
state are discarded on the redirect, without invalidating instruction-cache
lines.

Cold `Cpu::reset` now clears SR, distinguishing its initial state from NMI.
It also discards pending NMI requests. The core's deterministic reset values for
other registers remain unchanged.

## Clocks and devices

NMI entry consumes one modeled CPU cycle without retiring an instruction.
Count/Compare and RCP clock conversion continue across that boundary. Pending
store-buffer transactions retain their payloads and deadlines. A request from a
device callback during an instruction is serviced at the next step boundary.
The precise physical signal-sampling and pipeline-entry latency still need
hardware measurements; the one-cycle charge is the current scheduling model.

CPU NMI entry leaves RCP devices running. SI transfers, retained PIF channel
descriptors, device interrupts, EEPROM programming, and saved Pak bytes survive.
The core's existing frozen-bus state also remains set; redirecting the PC does
not complete an unresolved bus transaction.

## Validation and remaining scope

`tests/cpu/test_nmi.cpp` covers interrupt and privilege masks, normal exception
register preservation, direct and register jumps, taken and untaken branches,
annulled slots, nested requests, ERET, cache/TLB/register preservation,
prefetched instructions, clock progression, stalled-bus state, and cold reset.
`tests/rcp/test_nmi_devices.cpp` checks buffered stores, an in-flight SI command
under bulk/single-cycle CPU/RCP advances, EEPROM's busy deadline, saved bytes,
and a request made during audio delivery.

The [PIF boot controller](pif-boot.md) uses this event for checksum failures and
boot timeout. Reset-button input, pre-NMI notification, PIF warm-reset sequencing
and ROM unlocking, and warm reboot with supplied firmware remain unfinished
under #35. Exception timing captures remain under #8. These tests establish the
implemented state and event ordering, not a completed console reset sequence.
