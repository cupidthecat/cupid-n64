# PIF boot control

`PifBoot` owns the PIF's boot-command state, private seed/checksum bytes, ROM
lockout, and boot deadline. `Bus` advances it with the other RCP devices. CPU
stores and SI write DMA update external PIF RAM; the boot state machine observes
those bytes at a later poll boundary.

## Command sequence

Cold reset clears PIF RAM and exposes the configured CIC's two seed bytes at
offsets `0x26` and `0x27`. Offset `0x25` contains the version flag and, for a disk
security part, the disk flag. The 1,984-byte boot ROM remains readable.

The control byte at offset `0x3f` advances through these stages:

| Stage | Required bit | Result |
| --- | --- | --- |
| ROM lockout | `0x10` | Hide boot ROM and clear retained Joybus descriptors. |
| Checksum capture | `0x20` | Swap seed/OS information and six checksum bytes into private storage; set acknowledgement bit `0x80`. |
| Checksum comparison | `0x40` | Clear the control byte's upper nibble, compare all 48 checksum bits, and clear the captured checksum. |
| Boot termination | `0x08` | Clear the control byte, cancel the deadline, and enter normal operation. |

A bit for a later stage cannot skip an earlier stage. Combined bits are handled
at successive polls. Checksum capture occurs once: writing `0x20` again cannot
replace the captured bytes. The secret range begins at the low nibble of offset
`0x25`; its high nibble is preserved. Boot-command bits no longer restart the
sequence after termination. Joybus configuration remains a separate write event.

The ordering, private-memory exchange, command-nibble clearing, and error loop
follow the decoded PIF microcontroller program's `boot`, `memSwapRanges`, and
`signalError` routines. See the
[firmware reconstruction](https://github.com/GenericHeroGuy/pif-sm5-rom/blob/master/cmodel.c)
and its accompanying disassembly.

## Clocks and security failure

The current scheduler polls every 81,920 clocks of the 187.5 MHz system clock.
That gives intervals of 27,306 or 27,307 RCP cycles; the remainder
survives both bulk and single-cycle advances. Waiting for a command does not
require scheduling empty polls. SI completion writes become visible to the next
boot poll rather than being counted as present throughout the transfer.

A successful checksum comparison starts a six-second modeled deadline
(375,000,000 RCP cycles). Termination cancels it; a termination bit already
present when the deadline is serviced takes precedence over expiration. A bad
checksum or an expired deadline enters a persistent failure state. Each later
poll requests another CPU NMI, representing the firmware's repeating error
loop. PIF Joybus configuration and execution stop, while the RCP and its clocks
continue. Console reset clears the failure state and unlocks the ROM.

These intervals are scheduling approximations, not measured microcontroller
instruction timing. The decoded firmware describes a deadline of approximately
five seconds; the current six-second model still needs physical validation.
The NMI pulse rate is also approximate. SI still completes its modeled transfer
in the failure state; disabled PIF acknowledgements and resulting serial stalls
are not yet reproduced.

## Validation and remaining work

`tests/rcp/test_pif_boot.cpp` covers phase ordering, every checksum bit, combined
commands, secret boundaries, repeated requests, cold-reset recovery, SI DMA,
idle clock phase, and termination/expiration boundaries through CPU and RCP
advances. The configured-checksum fixtures cover all eleven security-part
profiles and both RAM sizes. They do not establish complete disk, arcade, PAL
firmware, or cartridge compatibility.

[Cartridge timing results](../testing/pif-boot-results.md) record the six changed
load-miss averages and the diagnostic build that isolates the boot-poll delay.

[Reset-button and warm-boot handling](warm-reset.md) restores the private seeds,
unlocks ROM, and requests NMI after pre-NMI and button release. Initial CIC wire
handshakes, region lockout, continuous CIC comparison, unknown bootcode policy,
and physical timing validation remain under issue #35. [CPU NMI](cpu-nmi.md)
describes exception entry itself. A supplied PIF boot ROM is still required to
execute the CPU's boot program.
