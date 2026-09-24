# Transfer Pak

Select `ControllerAccessory::TransferPak` through `Bus::set_controller_state`.
`Bus::transfer_paks[port]` owns the adapter and its optional Game Boy cartridge.
Construct a cartridge with `GameBoyCartridge::create(rom, config, error)`, then
move it into `TransferPak::insert`. A failed construction returns `std::nullopt`
and an error without changing an inserted cartridge. `remove()` empties the slot.

The [Game Boy cartridge model](game-boy-cartridge.md) supplies ROM, mapper
registers, save RAM, and clock registers. The adapter exposes that cartridge bus;
it does not run a Game Boy CPU or expose Game Boy internal RAM and peripherals.

## Registers and cartridge window

Attachment acknowledgement, address/data CRCs, packet lengths, and error flags
use the common [Pak transfer rules](controller-pak.md). Each accepted data byte
reaches the adapter in order. Register effects therefore follow every supplied
byte, including short writes. A full write acknowledges the first 32 data
bytes' CRC. Invalid address CRCs and pending attachment prevent all adapter
effects.

Address bit 15 is ignored inside the adapter:

| Pak address | Function |
| --- | --- |
| `0x8000` through `0x9fff` | Power control and identification |
| `0xa000` through `0xafff` | Game Boy window bank, zero through three |
| `0xb000` through `0xbfff` | Cartridge access control and status |
| `0xc000` through `0xffff` | Selected 16 KiB of the Game Boy cartridge bus |

The corresponding lower-half addresses are mirrors. The adapter starts disabled.
Writing `0x84` enables it; `0xfe` disables it. Other values leave power unchanged.
A disabled-to-enabled transition selects bank three, disables cartridge access,
and clears the reset state. Identification reads return `0x84` while enabled.
Reads while disabled return zero, and only power-control writes take effect.
Writing a bank value above three selects bank zero.

Bit zero of a status-register write controls cartridge access. A zero-to-one
transition resets the cartridge's mapper registers and starts reset state three.
Status reads contain access in bit zero, reset state in bits 3:2, cartridge
absence in bit six, and adapter enable in bit seven. Other bits are zero.
After each returned status byte, enabled access changes reset state three to
two. With access disabled, state two changes to one and one changes to zero.
A normal full status read after enabling a present cartridge starts `8d 89 89`.
After disabling access, it starts `88 84 80`. The state transitions occur per
byte, including when a short reply is requested.

With access enabled, the window maps to
`bank * 0x4000 + (pak_address & 0x3fff)`. An empty slot or disabled access returns
zero and ignores window writes. ROM and RAM banking inside the cartridge remains
separate from the adapter's four window banks. The
[libdragon Transfer Pak interface](https://libdragon.dev/ref/group__transferpak.html)
describes this distinction and the power/access initialization sequence.

## Lifetime and timing

Disconnecting the controller or changing its device/accessory disables the
adapter and resets mapper registers. Inserted ROM and save RAM remain available
for reattachment. Console reset preserves the adapter registers and cartridge
state. Removing a cartridge changes the absence flag; it does not remove the
adapter. Inserting a replacement resets that cartridge's mapper state.

A configured cartridge clock advances from RCP cycles even with adapter power
or access disabled. Battery clock state and latched registers survive mapper
power resets. Slot removal ends advancement of the removed cartridge. Clock
ticks run before SI completion at a shared deadline, so a latch command at that
boundary captures the advanced clock. Event scheduling in `Bus::next_event()`
queries `TransferPak::clock_running()` directly to check for active cartridge
clocks without indirect pointer dereferencing when no cartridge is inserted.

Commands execute at the current SI model's completion event. Individual serial
bits and Game Boy bus strobes are not scheduled. Hardware captures are still
needed for power-up delays, reset-state transitions, removal during a transfer,
address mirrors, and device-specific serial timing under #28 and #30.

## Validation

`tests/rcp/test_transfer_pak.cpp` and `test_transfer_pak_si.cpp` exercise
attachment, empty/present slots, power and reset states, all bank-register values,
mirrors, CRC rejection, short/padded/malformed packets, removal, reset, and save
preservation. Real mapper reads and writes pass through encoded Joybus packets
for every implemented mapper. CRCs use the shared polynomial-division oracle.

SI tests configure by CPU store or write DMA, then execute initialization,
bank switching, and save readback on read completion with bulk/single-cycle CPU
and RCP advances. A separate shared-deadline case
checks an RTC tick against an SI latch command. These tests establish the
implemented ordering; they do not establish physical bus timing or compatibility
with a released Transfer Pak title. The release corpus remains open under #38.
