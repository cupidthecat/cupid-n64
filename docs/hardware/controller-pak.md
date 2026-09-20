# Controller Pak detection

The core provides a separate 32 KiB Controller Pak array for each controller
port. Update inputs and connection state through `Bus::set_controller_state`.
`Bus::controllers()` exposes a read-only view; connection changes cannot bypass
the setter by modifying that view.

## Attachment and acknowledgement

Each newly constructed controller starts with its configured Pak attached and
detection pending. Removing or inserting a Pak sets that port's detection latch.
Reconnecting a controller with a Pak also sets it. Repeated input updates with
unchanged connection and Pak presence leave the latch alone.

Status commands `0x00` and `0xff` return `05 00` followed by:

| Status byte | Meaning |
| --- | --- |
| `0x03` | Pak present, attachment change not yet acknowledged. |
| `0x01` | Pak present, change acknowledged. |
| `0x02` | No Pak present. |

Executing either status command clears the latch. This also applies to short
or zero-length replies: acknowledgement is a command side effect, independent
of how many reply bytes the PIF copies. Polling buttons, malformed commands,
skipped packets, and channel-reset requests do not acknowledge a change. An
absent controller does not respond and cannot acknowledge its latch.

## Pak access and CRCs

While detection is pending, Pak reads return zero data and invert the data CRC;
a full zero block therefore returns CRC `0xff`. Writes return the inverted CRC
of their supplied data and leave the array unchanged. These requests still
receive a valid controller response rather than the PIF no-response flag.

No Pak present and an invalid address CRC use the same rejection behavior.
After status acknowledges an attached Pak, reads and writes use the existing
32-byte block and CRC rules. A bad address CRC does not clear or reassert the
detection latch. Changes and acknowledgements on one port do not affect another.

## Reset and storage lifecycle

Console reset preserves controller inputs, Pak presence, saved bytes, and both
pending and acknowledged detection state. Joybus channel-reset markers and
send-length reset flags also preserve this state for the supported controller.
They are distinct from the `0xff` status command, which acknowledges detection.

The per-port array survives removal and reinsertion. The core currently treats
that as reattaching the same stored Pak; it does not provide card identities or
save-file persistence. Replacing saved contents is separate from signalling
attachment through the controller setter.

## Validation and limits

`tests/rcp/test_controller_pak.cpp` covers all four ports, initial attachment,
removal and reinsertion, controller reconnection, status prefixes, access gating,
CRC rejection, repeated input updates, port isolation, and reset. SI tests check
acknowledgement at DMA completion in both directions under bulk and single-cycle
CPU/RCP advances. The existing parser and address-CRC tests acknowledge initial
attachment before exercising ordinary Pak access.

Additional accessory types, larger Pak banking, game-level persistence, and
hardware lifecycle captures remain unfinished under issue #29 and related
release work. [Joybus packets](joybus.md) describes packet boundaries and replies;
[serial-interface timing](serial-interface.md) records the current timing limits.
