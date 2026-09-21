# GameCube controller on an N64 port

The N64 controller bus can carry the GameCube controller protocol used by N64
homebrew. The core exposes this device as `ControllerDevice::GameCube` and takes
its inputs through `Bus::set_gamecube_state`. There is no frontend or command-line
mapping for it yet.

The public homebrew interfaces are documented by libdragon's
[joypad API](https://libdragon.dev/ref/group__joypad.html) and
[Joybus API](https://libdragon.dev/ref/group__joybus.html).

An embedding application selects the device and submits its raw input separately:

```cpp
cupid::ControllerState controller;
controller.device = cupid::ControllerDevice::GameCube;
system.bus.set_controller_state(0, controller);

cupid::GameCubeState input;
input.buttons0 = 0x01; // A button; sticks retain their neutral defaults.
system.bus.set_gamecube_state(0, input);
```

The port argument is zero-based, from 0 through 3. N64 accessory selections are
ignored for this device and do not affect its motor or origin handshake.

`GameCubeState` contains the raw values that appear on the controller wire. The
main stick and C-stick use unsigned bytes with 127 as neutral. The analog
triggers use unsigned bytes with zero as their resting value. `buttons0` uses
bits 0 through 4 for A, B, X, Y, and Start. `buttons1` uses bits 0 through 6 for
Left, Right, Down, Up, Z, R, and L. Higher button bits are discarded by the
core because bit 5 of the first response byte and bit 7 of the second response
byte are protocol flags. Host deadzones, axis calibration, trigger-click
conversion, and input mapping belong outside the controller protocol.

The status commands `0x00` and `0xff` identify a standard GameCube controller as
`0x0900`. Status bit 3 reports the current rumble motor state. Command `0x40`
requires three request bytes. Its third byte bit 0 drives rumble, while the
second byte selects the analog packing mode. The first four response bytes are
the two button bytes and the main-stick X/Y values. Bytes 4 through 7 are packed
as follows:

| Mode | Byte 4 | Byte 5 | Byte 6 | Byte 7 |
| --- | --- | --- | --- | --- |
| 0, 5, 6, 7 and other values | C-stick X | C-stick Y | L high nibble, R high nibble | 0 |
| 1 | C-stick X/Y high nibbles | L | R | 0 |
| 2 | C-stick X/Y high nibbles | L/R high nibbles | 0 | 0 |
| 3 | C-stick X | C-stick Y | L | R |
| 4 | C-stick X | C-stick Y | 0 | 0 |

The standard pad has no analog A/B inputs, so those fields are zero in every
mode that carries them. Replies longer than eight bytes set the Joybus overflow
flag.

Commands `0x41` and `0x42` return the ten-byte origin block and clear the
controller's pending-origin flag. The fixed neutral origin is 127 for both
sticks and zero for both triggers and analog face-button fields. Command `0x43`
returns the ten-byte long state without clearing the pending-origin flag.
Origin and long-read replies set overflow when the requested receive length is
greater than ten bytes. Unknown commands, and `0x40` requests shorter than
three bytes, are rejected with the normal Joybus no-response flag.

The pending-origin flag starts set for each attached GameCube controller. It is
re-armed when a GameCube device is connected or selected again. PIF channel
reset descriptors and the alternate Joybus reset flag re-arm it and stop that
port's rumble motor. The alternate skip flag takes priority when skip and reset
are both present. Reconfiguring a PIF channel alone does not reset the physical
controller. A console reset likewise leaves the attached controller's raw
state, origin handshake state, and motor state alone until the PIF sends a
controller reset or the device is unplugged or replaced.

The current SI timing model charges a connected-controller packet at the same
rate as the existing N64 controller path. A single GameCube packet on zero-based
port `p`, followed by the packet terminator, completes after `37020 + 1420 * p` RCP
cycles. A `0xfd` channel-reset descriptor followed by the terminator completes
after `16440 + 1420 * p` RCP cycles. PIF configuration records the descriptor;
the controller state change happens when the later SI read transfer completes.
These values are emulator scheduling estimates. They are not measurements of
the controller's serial bit timing. Independent hardware captures are still
needed for that timing and for electrical edge cases.

`tests/rcp/test_gamecube.cpp` covers all four controller ports and all eight
analog mode values, exact status/origin/long-read payloads, overflow and NAK
boundaries, raw button masking, origin reset paths, rumble isolation, hotplug
and type changes, and SI completion timing in both CPU and RCP clock domains.
