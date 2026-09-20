# Bio Sensor

Select `ControllerAccessory::BioSensor` on a gamepad through
`Bus::set_controller_state`. Each port has an independent pulse input, initially
inactive. `Bus::set_bio_sensor_pulse(port, active)` supplies its current level.
Input for an invalid port, a disconnected controller, a mouse, or another
accessory is ignored.

The input remains at that level until the caller changes it. Reads do not consume
a pulse. The core does not generate a heartbeat, choose a pulse duration, or
consult the host clock. A caller can supply physical sensor samples or a
deterministic sequence of levels.

## Protocol

The sensor uses the gamepad's status and Pak commands. Attachment must be
acknowledged with status command `0x00` or `0xff` before Pak transfers succeed.
The common [address and data CRC rules](controller-pak.md) apply.

| Byte address | Read value |
| --- | --- |
| `0x0000` through `0x7fff` | `0x00` |
| `0x8000` through `0xbfff` | `0x81` |
| `0xc000` through `0xffff` | `0x00` during a pulse; `0x03` otherwise |

[Published Bio Sensor measurements](https://www.raphnet.net/divers/n64_bio_sensor/index_en.php)
record repeated 32-byte reads at `0xc000`, with `0x03` between heartbeats and
`0x00` during a pulse. Those observations support the pulse register; they do
not independently verify every address alias or the identification range.
The [libdragon accessory definitions](https://libdragon.dev/ref/joybus__accessory__internal_8h.html)
also identify `0x81` as the Bio Sensor probe value at `0x8000`.

Reads return up to 32 data bytes, followed by their CRC when the requested reply
has room. Extra reply bytes are zero. Writes acknowledge the supplied data CRC
and leave the sensor input and Controller Pak storage unchanged. Short writes
with at least one data byte return CRC zero. Bad address CRCs, pending attachment,
and an absent accessory reject transfers through an inverted data CRC. Malformed
lengths and unsupported commands use the normal no-response behavior.

## Input lifetime and timing

Disconnecting a controller, changing its device type, or replacing its accessory
clears that port's pulse input. Reattachment begins inactive and requires status
acknowledgement. Updates to gamepad buttons and stick values preserve the pulse.
Console reset, status, polling, rejected commands, and writes also preserve it.
Changing a different port has no effect.

The SI model executes the Pak command at its completion event. A pulse level
supplied while DMA is pending is therefore sampled at completion. All bytes of
that reply use the same level. The model does not represent a pulse transition
between individual serial reply bits.

## Validation and limits

`tests/rcp/test_bio_sensor.cpp` covers every aligned read and write block on all
four ports, both pulse levels, attachment detection, address and data CRCs,
short and padded transfers, malformed and unsupported commands, saved-data
preservation, device changes, port isolation, and reset. The CRC checks use the
shared polynomial-division oracle.

An integration sequence acknowledges attachment, reads identification, and
samples inactive/active/active/inactive pulse levels through both SI DMA
directions. Bulk and single-cycle CPU/RCP advancement check pending responses,
input changes immediately before completion, final replies, and interrupts.

Identification aliases, ignored-write behavior, pulse transitions within a
transaction, and accessory-specific serial timing still need hardware captures.
Tetris 64 compatibility and a host sensor binding have not been validated. The
accessory support matrix remains open under #30; physical SI timing remains
open under #28.
