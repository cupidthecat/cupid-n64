# Desktop input mapping

The desktop input layer is host-only. It does not include SDL headers and does
not read or write settings files. A platform adapter supplies keyboard and
gamepad state to `cupid::host::InputMapper`, then copies the mapper's four
`ControllerState` values into the core. The mapper changes only `buttons`,
`stick_x`, and `stick_y`; the configured connection state, controller device,
and accessory are preserved.

## Host input model

Keyboard scancodes are integers from 0 through 511. `-1` is used only inside a
binding to mean unbound. Runtime keyboard events outside 0 through 511 are
rejected.

Gamepads use a platform-neutral instance id plus normalized button and axis
enums. Axis samples are signed 16-bit values. Stick axes use the full signed
range, with `-32768` normalized to `-32767` so inversion is symmetric. Trigger
bindings use the same axis representation and normally use positive values.

At most four gamepads are tracked. A newly connected gamepad is assigned to the
lowest numbered gamepad-enabled port that does not already have a gamepad. An
existing assignment is not shuffled when another gamepad is removed. If a pad
was connected while no eligible port was free, it claims the first port that
later becomes free. Instance ids are runtime identities and are not serialized.

Losing window focus clears every live key, button, and axis level. Input events
received while unfocused cannot re-arm a held control. After focus returns, the
adapter must provide fresh input state before a control becomes active again.
This prevents stuck buttons or sticks after task switching.

The SDL adapter also checks connection state while polling. If a gamepad stops
reporting as connected before its removal event is delivered, the adapter drops
the assignment and clears its live state immediately. A later SDL removal event
is harmless. This prevents a held button or stick from remaining latched during
an unplug race.

## Default bindings

Port 1 accepts both keyboard and gamepad input. Ports 2 through 4 accept
gamepads by default; keyboard input can be enabled per port. Every binding is
rebindable.

The keyboard defaults are:

| N64 control | Scancode/key |
| --- | --- |
| Analog stick | W/A/S/D (`26/4/22/7`) |
| A | X (`27`) |
| B | Z (`29`) |
| Start | Enter (`40`) |
| Z | Left Shift (`225`) |
| L / R | Q / E (`20/8`) |
| D-pad | Arrow keys (`82/81/80/79`) |
| C Up/Down/Left/Right | I/K/J/L (`12/14/13/15`) |

The gamepad defaults follow an Xbox-style layout: South is N64 A, West is N64
B, Start is Start, left trigger is Z, the shoulder buttons are L/R, and the
gamepad D-pad is the N64 D-pad. The right stick provides the four C buttons.
The default C threshold is 16000 and the Z trigger threshold is 12000.

The left stick uses a radial deadzone and radial output clamp. Defaults are a
4096 deadzone, 32767 input range, and 80-unit N64 output range. Full diagonal
input is normalized inside the same output radius rather than producing 80 on
both axes. The Y axis is inverted by default because common desktop gamepad
APIs report downward motion as positive. X/Y inversion, deadzone, input range,
output range, source axes, and keyboard directions are configurable per port.

## N64 button bits

The mapper produces only controller button bits that the N64 pad exposes:

| Control | Mask |
| --- | ---: |
| A | `0x8000` |
| B | `0x4000` |
| Z | `0x2000` |
| Start | `0x1000` |
| D-pad Up/Down/Left/Right | `0x0800 / 0x0400 / 0x0200 / 0x0100` |
| L / R | `0x0020 / 0x0010` |
| C Up/Down/Left/Right | `0x0008 / 0x0004 / 0x0002 / 0x0001` |

The controller core continues to own reserved-bit filtering, opposing D-pad
handling, and the L+R+Start reset chord.

## Saved bindings

`encode_input_bindings` and `decode_input_bindings` provide a deterministic,
versioned text representation. Version 1 begins with `CUPID_INPUT 1`, contains
one port and stick record plus fourteen button records for each of the four
ports, and ends with `END`. The decoder accepts at most 16 KiB, validates every
enum, scancode, threshold, deadzone, and range before narrowing values, rejects
missing or trailing records, and only replaces the caller's `InputBindings`
after the complete document validates. A malformed document therefore cannot
partially change live settings.

This module deliberately has no settings path or file writer. The frontend can
persist the encoded text through the shared host atomic-file mechanism used by
desktop settings, avoiding a second replacement implementation in the input
layer.

Tests in `tests/host/test_input.cpp` cover exact N64 masks, analog deadzones and
diagonal bounds, inversion and range changes, keyboard/gamepad combination,
four-port isolation, hotplug removal and reassignment, focus loss, preservation
of configured hardware fields, strict malformed-settings rejection, and exact
serialization round trips.
