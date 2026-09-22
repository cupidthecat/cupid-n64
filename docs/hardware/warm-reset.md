# Reset button and warm boot

`System::set_reset_button(bool pressed)` supplies the physical reset-button
level on the emulation thread. A new press starts a reset only after the PIF has
accepted boot termination. A held button does not retrigger; presses during
boot, a pending reset, or security failure do not start another sequence.

## Pre-NMI and release

An accepted press asserts pre-NMI on CPU Cause.IP4 (`0x1000`). This is a maskable
interrupt: IE, EXL, ERL, and the IP4 interrupt mask determine whether the CPU
takes an exception. Cause writes cannot acknowledge the input. The CPU samples
it at instruction boundaries and after advancing connected hardware clocks,
alongside the MI interrupt input. Software and timer interrupt bits are retained.
The [libdragon interrupt handler](https://github.com/DragonMinded/libdragon/blob/trunk/src/inthandler.S)
documents the same IP4 routing and persistent pending state.

The PIF waits 31,250,000 RCP cycles (0.5 seconds), then waits until the button is
released. An early release still waits for the deadline. Holding the button
past the deadline keeps pre-NMI asserted and the ROM locked. Release after the
deadline is serviced on the next RCP clock boundary. Repeated presses cannot
extend the existing deadline. During the delay, the PIF refreshes its control
byte's acknowledgement bit.

Changing the reset-button level first settles earlier device time, then
invalidates the cached scheduler deadline. This matters when a held button is
released after the timeout: cached CPU execution must see the new one-RCP-cycle
deadline immediately. Retaining the held-button deadline would let further
instructions execute before the PIF requests NMI.

Nintendo's [reset requirements](https://jrra.zone/n64/doc/caution/caution/index6.htm)
guarantee at least 0.5 seconds between pre-NMI and NMI and require the game to
prepare the RSP, audio, graphics, and VI state. Cupid-N64 uses that minimum as
the current delay. It does not yet reproduce the CIC wire handshake, oscillator
variation, or individual PIF instruction times. The exact physical delay and
acknowledgement timing remain unverified.

## Warm entry

Once the delay and release conditions are satisfied, the PIF clears pre-NMI,
unlocks the boot ROM, and requests [CPU NMI](cpu-nmi.md). Its private OS flags
and seeds return to external PIF RAM, including the warm-start flag. The
checksum range is exchanged with private memory, and the control byte clears.
Other PIF RAM remains intact. Boot-command polling starts again with a fresh
poll phase. The supplied CPU boot firmware then runs the lockout/checksum/
termination sequence again before the button is enabled for another reset.

The memory exchange and release ordering follow `interruptB`, `cicReset`, and
`boot` in the [decoded PIF firmware](https://github.com/GenericHeroGuy/pif-sm5-rom/blob/master/cmodel.c).
The PIF does not call `System::reset`, reset RCP devices, invalidate CPU caches,
or clear RDRAM. Device transfers, store-buffer work, saved data, RAM hidden bits,
and configured RAM chips remain available. Clocks continue during both the
pre-NMI interval and NMI entry. Game software is responsible for stopping work
that cannot safely cross reset.

`System::reset()` remains the cold-reset operation. It cancels a pending button
sequence and restores cold boot flags. The supplied physical button level is
retained, so a button held through cold reset needs release and a new press
after boot termination to start another reset.

## Validation and limits

Seventeen regressions in `tests/rcp/test_warm_reset.cpp` cover the input edge,
disabled boot stages, held/released buttons, deadline partitioning through CPU
and RCP clocks, interrupt masks, Cause writes, simultaneous interrupt sources,
repeated resets, cold-reset cancellation, and all configured security-part
seeds with both RAM sizes. Integration cases preserve RAM/hidden bits, device
registers and saved bytes, an in-flight SI transfer, EEPROM programming, and
active RSP execution. An audio callback checks an input change during a CPU
instruction.

`tests/rcp/test_pif_reset_deadline.cpp` releases the button after the timeout
and executes ordinary cached CPU steps without an intervening scheduler query.
It checks that NMI stops the next instruction and records its address in
ErrorEPC. The same regression fails on the previous scheduler, which executes
one extra instruction before noticing the release.

A supplied NTSC PIF and Super Mario 64 cartridge also reach the game's entry
point after warm reset with both 4 and 8 MiB RAM. The firmware reports a warm
start and preserves a marker in the standard NMI buffer. These runs establish
that firmware can boot again; they do not establish every game's reset behavior
or validate other region, disk, or arcade firmware.

`tests/boot/warm_reset.cpp` runs the complete configured test cartridge before
and after a reset-button sequence. It checks the pre-NMI deadline, preserved
NMI-buffer marker, warm-start flag, ROM unlock, and reset-button rearming. Both
runs must report the same nonzero number of tests with no failures. CTest adds
this check as `nemu64_warm_reset` when `CUPID_TEST_ROM` is supplied. The pinned
cartridge passes all 4,637 cases in each run on the 8 MiB configuration. Its
allocator uses a fixed 7 MiB heap endpoint, so that cartridge cannot validate a
4 MiB machine; the smaller configuration is covered by the unit and separate
game-boot checks above.

Initial and continuous CIC handshakes, region lockout, unknown bootcode policy,
physical timing captures, and serial failure stalls remain under issue #35.
