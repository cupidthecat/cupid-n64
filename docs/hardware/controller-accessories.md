# Controller accessories

`ControllerState::device` selects an N64 `Gamepad`, [Mouse](mouse.md), or
[GameCube controller](gamecube-controller.md) on each port. The accessory
selections below apply to N64 gamepads. Mouse and GameCube input use separate
APIs and do not expose an N64 Pak socket.

Each gamepad port has one accessory selection in `ControllerState::accessory`:

| Selection | Implemented behavior |
| --- | --- |
| `ControllerAccessory::None` | Gamepad status/polling; Pak commands return rejected data CRCs. |
| `ControllerAccessory::ControllerPak` | One through 62 banks of 32 KiB per port, bank selection, block reads/writes, address/data CRCs. |
| `ControllerAccessory::RumblePak` | Identification reads, motor commands, motor-state readback, address/data CRCs. |
| `ControllerAccessory::BioSensor` | Identification reads, externally supplied pulse levels, ignored writes, address/data CRCs. |
| `ControllerAccessory::TransferPak` | Power/access registers and a banked Game Boy cartridge bus, including supported mapper RAM and clocks. |

The default remains a connected gamepad with a Controller Pak. Update the
selection and input state through `Bus::set_controller_state`; inspect it through
`Bus::controllers()`. `Bus::rumble_active(port)` reports the commanded motor
state and returns false for an invalid port. It does not drive host haptic
hardware; that requires a frontend binding.

`ControllerState` and `ControllerAccessory` are declared in
`include/cupid/rcp/controller.hpp`, which `bus.hpp` includes. The previous
`controller_pak` boolean has been removed. Existing callers should replace
`controller_pak = true` with `accessory = ControllerAccessory::ControllerPak`
and `controller_pak = false` with `accessory = ControllerAccessory::None`.
The single selection permits one accessory per port. [Bio Sensor](bio-sensor.md)
pulse input uses `Bus::set_bio_sensor_pulse` independently of gamepad buttons.

## Attachment and motor lifetime

Changing accessories sets that port's detection latch. Reconnecting a controller
with an accessory also sets it. Status commands `0x00` and `0xff` report `05 00`
with status `0x03` for pending attachment, `0x01` after acknowledgement, or
`0x02` with no accessory. Either status command acknowledges attachment even
when its reply is too short to contain the status byte.

Pending detection rejects Pak reads/writes through the data CRC. Read data is
zero, and the CRC is inverted. A rejected write inverts the supplied data CRC
without changing memory or motor state. Invalid address CRCs and an absent
accessory use the same behavior. A disconnected controller instead leaves the
reply untouched and sets the PIF no-response flag.

The motor starts off. Removing or replacing a Rumble Pak, or disconnecting its
controller, stops that motor immediately. Reattachment starts off and requires
status acknowledgement before motor commands can take effect. Changing another
port or updating buttons/stick samples does not affect an active motor.

Console reset preserves accessory selection, detection state, saved Pak bytes,
and motor state. Status, polling, channel-reset/skip requests, malformed commands,
and the L+R+Start stick-reset combination do not stop a motor. A motor write or
disconnection is needed to change it. Switching between accessories retains
the per-port Controller Pak storage. Card identities are not modeled. The runner
can persist the complete configured Pak through `--pak-file`; see
[Controller Pak behavior](controller-pak.md) for banking and
[persistent storage](storage.md) for the raw file layout.

## Rumble Pak protocol

Pak reads (`0x02`) use the following byte values after a valid address CRC and
attachment acknowledgement:

| Address range | Read value |
| --- | --- |
| `0x0000`–`0x7fff` | `0x00` |
| `0x8000`–`0x8fff` | `0x80`, independent of motor state |
| `0x9000`–`0xffff` | `0xff` while enabled; `0x00` while disabled |

Pak writes (`0x03`) at `0xc000` and above set the motor from bit zero of the first
data byte. Other bits and later bytes do not determine motor state, although
all first 32 data bytes contribute to a full write's CRC. Writes below `0xc000`
acknowledge the data without changing the motor or Controller Pak storage.

The common [Pak transfer rules](controller-pak.md) apply: addresses encode a
32-byte-aligned block and five CRC bits, reads supply at most 32 data bytes
plus a CRC, and writes use at most 32 bytes. Short writes with at least one
data byte can operate the motor but return CRC zero. Invalid short writes
return `0xff` and leave the motor unchanged. Extra reply bytes are zero.
Malformed command/reply lengths set the no-response flag and preserve motor state.

## Validation and support limits

`tests/rcp/test_rumble_pak.cpp` covers every encoded read/write block on all
four ports, motor bit selection, identification, address/data CRCs, short and
padded transfers, malformed lengths, attachment changes, port isolation, and
reset. The CRC checks use the shared polynomial-division test oracle.

An integration sequence acknowledges attachment, writes the identification
range, reads the Rumble Pak ID, starts the motor, reads its state, and stops it.
CPU stores or write DMA configure requests; read DMA executes them with bulk/single-cycle CPU and RCP
advances, checking state immediately before and at each completion boundary.
These deadlines come from the current SI model; accessory-specific hardware
transaction traces remain unfinished under #28.

The [cartridge RTC](cartridge-rtc.md) is selected separately on Joybus channel
four and can coexist with any cartridge save chip.

The [Transfer Pak](transfer-pak.md) owns an optional
[Game Boy cartridge](game-boy-cartridge.md) for each port. Its cartridge mapper
selection is independent of the adapter's 16 KiB window bank.

Voice hardware and 64DD are
not implemented or selectable as these accessories. The core does not claim
their protocols or identify them as Rumble Paks. Unsupported Joybus commands
retain the response bytes and set the no-response flag. The release compatibility
corpus and required device list remain unfinished under #30 and #38; the table
above describes current core support rather than a completed release matrix.
