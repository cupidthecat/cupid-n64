# Nintendo 64 mouse

Set `ControllerState::device` to `ControllerDevice::Mouse` through
`Bus::set_controller_state` to select a mouse on any controller port. The default
device remains `Gamepad`. `connected` controls whether the selected device
responds. `accessory`, `buttons`, and the stick coordinates apply to gamepads;
they do not supply mouse input or attach a Pak to a mouse.

Submit relative motion and button levels through `Bus::add_mouse_input(port,
MouseInput)`. The `left` and `right` fields replace the current button levels.
`delta_x` and `delta_y` add movement since the caller's last update, with positive
X pointing right and positive Y pointing down. Multiple updates accumulate
before a poll. The input queue saturates at signed 32-bit limits; its arithmetic
does not wrap. Updates to invalid ports, disconnected devices, or gamepad ports
are ignored. Host capture, sensitivity, and operating-system event delivery
remain frontend responsibilities.

## Joybus behavior

Status commands `00` and `ff` currently return `02 00 02`: mouse type followed by
the no-accessory status. They preserve pending motion and buttons. Short replies
receive the requested prefix; extra bytes are zero. Status does not set the
reply overflow flag.

Poll command `01` returns four bytes:

| Byte | Meaning |
| --- | --- |
| 0 | Left button in bit 7, right button in bit 6; other bits zero |
| 1 | Zero |
| 2 | Signed X movement, clamped to -128 through 127 |
| 3 | Signed Y movement after direction reversal, clamped to -128 through 127 |

A poll consumes both pending motion totals, including any movement beyond the
clamp limits. The next poll returns zero movement until new input arrives.
Button levels remain held. Short polls still sample and consume both axes,
including a poll requesting no reply bytes. Replies longer than four bytes have
zero padding and set the PIF overflow flag. Extra command bytes are ignored.

All other commands, including Pak reads and writes, leave reply bytes untouched
and set the no-response flag. They preserve mouse input, saved Controller Pak
bytes, and motor state. A disconnected mouse uses the same no-response behavior.

## Input lifetime and timing

Changing the selected device or disconnecting it clears queued mouse motion and
button levels. Device changes also stop that port's rumble motor. Switching back
to a gamepad sets its Pak attachment latch, so the next status request acknowledges
the accessory. Saved Controller Pak bytes remain per-port storage across changes.

Ordinary state updates while a mouse remains connected, status/reset commands,
PIF channel skips, and console reset preserve its pending input. A poll is the
operation that consumes movement. Input is sampled when the Joybus command
executes at SI read completion; updates received while a DMA is pending
are included in that sample. The existing SI timing estimates apply to connected
mouse ports.

## Validation and unresolved hardware details

`tests/rcp/test_mouse.cpp` checks identification on every port and accessory
selection, all signed 16-bit axis values, clamp boundaries, button combinations,
motion consumption, input accumulation, signed-limit safety, short/padded replies,
unsupported commands, reset, disconnect/reconnect, device changes, port isolation,
and a mixed gamepad/mouse/RTC packet. SI sequences identify the mouse and poll
motion after configuration by CPU store or write DMA, with bulk/single-cycle CPU
and RCP advances through read completion.
They check the state just before and at completion and verify a second poll
returns zero movement.

The status byte still needs a captured hardware transaction: published
[mouse protocol notes](https://sites.google.com/site/consoleprotocols/home/nintendo-joy-bus-documentation/n64-specific/mouse)
list `02 00 00`, whereas the current model returns `02 00 02`. The tests record
the implemented response and do not resolve that discrepancy. Device-specific
serial timing, overflow behavior on physical hardware, and a compatible
cartridge or disk integration case also remain unfinished under #28, #30, and
#38. Supporting the mouse protocol does not provide a 64DD implementation.
