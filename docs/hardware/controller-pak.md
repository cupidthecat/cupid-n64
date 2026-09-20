# Controller Pak behavior

The core provides a separate 32 KiB Controller Pak array for each controller
port. Update inputs and connection state through `Bus::set_controller_state`.
`Bus::controllers()` exposes a read-only view; connection changes cannot bypass
the setter by modifying that view.

Select `ControllerAccessory::ControllerPak` in `ControllerState::accessory`.
The old `controller_pak` boolean is replaced by this explicit selection; use
`ControllerAccessory::None` to remove the Pak. [Controller accessories](controller-accessories.md)
also describes Rumble Pak selection and the shared attachment rules.

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

Each request encodes a 32-byte-aligned address in the upper 11 bits and an
address CRC in the low five bits. The address polynomial is `0x35`; the data
polynomial is `0x185`, including the leading term in each notation. Data CRCs
cover 32 bytes followed by eight zero bits.

The supported Pak has one 32 KiB bank. Addresses `0x0000` through `0x7fe0`
select its blocks. Reads at `0x8000` and above return zero data with a valid CRC;
writes to that range acknowledge the supplied data without changing storage.
The bank-select address `0x8000` cannot select another bank in this configuration.
Addresses do not wrap back into the array.

A read returns up to 32 data bytes and supplies the CRC only when at least
33 reply bytes were requested. A write stores at most the first 32 supplied
data bytes. Short writes leave the rest of the block unchanged and return CRC
zero; a rejected short write returns `0xff`. Extra write data is ignored, and
extra reply bytes after the CRC are zero. Reads need three command bytes,
writes need at least four, and both require at least one reply byte. Requests
that do not meet these lengths leave storage and the response area unchanged
and set the PIF no-response flag.

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
acknowledgement at read completion after configuration by CPU store or write
DMA, under bulk and single-cycle CPU/RCP advances. The existing parser and address-CRC tests acknowledge initial
attachment before exercising ordinary Pak access.

`tests/rcp/test_controller_pak_transfers.cpp` verifies all 8,192 port/address
pairs and 40,960 single-bit address-CRC corruptions with a polynomial-division
oracle. Data checks include fixed vectors, every one-hot bit in a block, and
256 deterministic mixed blocks. Boundary and malformed-length tests compare
all four storage arrays, including after reset. Last-block reads and writes
also run through SI write/read sequences with bulk/single-cycle CPU and RCP
advances, checking storage immediately before and at read completion.

Additional accessory types, larger Pak banking, and game-level persistence
remain separate release work. Hardware transaction timing remains open under
issue #28. [Retained PIF descriptors](joybus.md) are implemented. These tests establish the supported
core's packet and storage behavior; they do not substitute for hardware traces
or a game-save persistence test.
[Joybus packets](joybus.md) describes packet boundaries and replies;
[serial-interface timing](serial-interface.md) records the current timing limits.
