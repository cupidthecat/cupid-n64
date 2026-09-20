# Joybus packets

The core handles four controller ports followed by the cartridge channel. Packets
occupy PIF RAM bytes 0 through 62; byte 63 is the PIF control byte. Each request
contains a send length, a receive length, command bytes, and a response area.
The low six bits of each length determine the packet extent. A packet that
reaches the control byte or extends beyond it does not execute.

## Channel selection

| Byte or flag | Behavior |
| --- | --- |
| `0xff` marker | Skip padding without advancing the channel. |
| `0x00` marker | Advance to the next channel. |
| `0xfd` marker | Reset the channel and advance. |
| `0xfe` marker | End the packet list. |
| Send-length bit 7 | Skip the declared packet without executing its command. |
| Send-length bit 6 | Reset the channel without executing the declared command. |

Flagged packets still consume their complete send and receive areas. Their
response bytes and receive-status flags remain unchanged. Bit 7 takes priority
when both send flags are set. Reset requests preserve the supported controller
state, Controller Pak contents, and pending detection latch. A flagged cartridge
packet cannot start an EEPROM write.

## Replies and error flags

Device replies are built separately from PIF RAM. Only the requested receive
bytes are copied back, so a short reply cannot overwrite the following packet.
Valid replies are zero-padded beyond the bytes supplied by the device. Absent
devices and unsupported commands leave the response area unchanged and set
receive bit 7. Valid commands replace stale receive flags; a controller poll
requesting more than four bytes sets receive bit 6 for overflow.

Controller status commands `0x00` and `0xff` supply `05 00` followed by `03` for
an unacknowledged Pak attachment, `01` for an acknowledged present Pak, or `02`
for no Pak. Status acknowledges [Pak detection](controller-pak.md), including
when the requested reply is too short to contain that byte. Poll command `0x01` supplies the
button word followed by the signed X and Y stick bytes. Short requests return
the corresponding prefix, including a zero-length reply. Controller Pak
commands retain their address/data CRC and data-access rules; extra reply bytes
after the data or CRC are zero. Cartridge EEPROM replies are described in
[EEPROM behavior](eeprom.md). The optional [cartridge RTC](cartridge-rtc.md)
responds to commands `06`, `07`, and `08` on the same channel independently of
EEPROM presence and busy state.

The selected [controller accessory](controller-accessories.md) supplies Pak
read/write behavior. Controller Pak, Rumble Pak, and [Bio Sensor](bio-sensor.md) share status, detection,
CRC, packet-length, and error-flag handling.

Selecting a [mouse](mouse.md) changes status and polling to the mouse protocol
and rejects Pak commands. Mouse polls consume pending relative motion while
retaining button levels.

## Controller polling

`ControllerState::buttons` supplies the host's button inputs. Polling clears
the reserved ground and reset bits (`0x0040` and `0x0080`); the controller
generates the reset bit itself. Opposing D-pad directions cancel independently
on each axis. Other buttons, including opposing C buttons, remain independent.

Holding L, R, and Start together returns both stick bytes as zero, sets reset
bit `0x0080`, and clears Start in the response. L and R remain pressed. Releasing
the combination restores the supplied axis values and clears reset. Forming
the reply does not change the stored host inputs or acknowledge Pak detection.
The signed stick values are already controller samples; the core does not
apply host-device deadzones or an analog gate calibration.

## Validation and limits

`tests/rcp/test_joybus.cpp` checks skip/reset flags, zero-payload flagged packets,
all four controller ports, absent devices, reply prefixes and padding, stale
error flags, protected neighboring packets, Controller Pak write suppression,
and the PIF control-byte boundary. The same mixed controller/cartridge packet
runs through CPU PIF writes and both SI DMA directions under bulk and single-cycle
CPU/RCP advances. `tests/rcp/test_si.cpp` separately checks the existing DMA
deadlines for normal, reset, and skipped cartridge requests.

`tests/rcp/test_controller_poll.cpp` checks reserved bits, all 16 D-pad
combinations, all eight L/R/Start combinations, reset release, and short/padded
reset replies on all four ports. Controller Pak addressing, CRCs, storage
isolation, and reset are covered by the [Pak tests](controller-pak.md).

Packet parsing still runs as part of the core's command execution path. It does
not reproduce the PIF's internal instruction timing or retained channel
descriptors. Serial delays remain estimates, and hardware transaction captures
remain unfinished under issue #28. Additional accessory types remain separate
release work. See
[serial-interface timing](serial-interface.md).
