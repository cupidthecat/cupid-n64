# Game Boy cartridges in the Transfer Pak

`GameBoyCartridge` models the cartridge bus used by the [Transfer Pak](transfer-pak.md).
`GameBoyCartridgeConfig` explicitly selects the mapper, RAM capacity, clock
oscillator, and MBC5 rumble wiring. The factory checks these settings against
the supported capacities. It accepts power-of-two ROM sizes from 32 KiB upward;
unsupported sizes and incompatible peripheral combinations return an error.
Header interpretation and release-runner selection remain separate work under #33.

| Mapper | Maximum ROM | RAM |
| --- | --- | --- |
| Linear | 32 KiB | Unbanked, up to 8 KiB |
| MBC1 | 2 MiB | Up to 32 KiB with ROM no larger than 512 KiB; otherwise up to 8 KiB |
| MBC2 | 256 KiB | Built-in 512 nibbles, stored as 256 packed bytes |
| MBC3 | 2 MiB | Up to 32 KiB |
| MBC30 | 4 MiB | Up to 64 KiB |
| MBC5 | 8 MiB | Up to 128 KiB, or 64 KiB with rumble wiring |

External RAM capacity is zero or a power of two from 2 KiB through the mapper's
limit. MBC2's `ram_bytes` setting must be zero because its RAM is built in.
Unconnected address lines mirror installed ROM and RAM. ROM reads expose
cartridge bytes directly, including the header, without a Game Boy boot ROM.
Addresses outside `0x0000..0x7fff` and `0xa000..0xbfff` return zero and ignore
writes. Missing linear RAM also returns zero. Disabled or absent banked RAM
returns `0xff`.

## Banking and save data

[MBC1](https://raw.githubusercontent.com/gbdev/pandocs/master/src/MBC1.md)
uses a five-bit ROM bank, a two-bit secondary bank, and a banking mode. Bank zero
remaps to one before installed-ROM masking. Mode one applies the secondary
bank to the lower ROM window and selects banked RAM; mode zero fixes those
windows to bank zero. RAM enable compares the written low nibble with `0xa`.

[MBC2](https://raw.githubusercontent.com/gbdev/pandocs/master/src/MBC2.md)
uses address bit eight to choose RAM enable or its four-bit ROM bank register.
Its 512 nibble locations repeat throughout the RAM window. Reads supply a high
nibble of `0xf`; writes replace only the addressed low nibble. That high-nibble
choice is part of this model, not a guarantee about every physical cartridge.

MBC3 selects seven ROM-bank bits and MBC30 selects eight; zero maps to one.
Their RAM/clock selector retains four bits. Banks above the RAM range and
outside clock registers eight through twelve return `0xff`.

[MBC5](https://raw.githubusercontent.com/gbdev/pandocs/master/src/MBC5.md)
has independent low-eight and high-one ROM-bank registers, and bank zero is
usable. Without rumble, four bits select RAM. With rumble, bit three commands
the motor and only the low three bits select RAM. `rumble_active()` exposes
that command without driving host haptics.

`ram()` exposes the save bytes for import/export. Mapper power resets disable
banked RAM, select ROM bank one, clear RAM/mode registers, and stop cartridge
rumble while preserving save bytes. The core initializes new RAM to zero;
battery-file persistence and cartridge identity are not implemented here.

## MBC3 clock

The [MBC3/MBC30 clock registers](https://raw.githubusercontent.com/gbdev/pandocs/master/src/MBC3.md)
hold seconds, minutes, hours, a nine-bit day count, halt, and sticky day carry.
`set_clock` supplies initial state. No host clock or offline elapsed-time policy
is implicit. The `clock` configuration flag enables oscillator advancement;
the mapper's clock registers remain readable and writable without it.

RAM enable gates clock reads and writes. Writing zero then one to the latch
register snapshots the counters; reads retain that snapshot until another latch.
Register writes change the live counters. Seconds/minutes retain six bits,
hours five, and days nine. Unused day-high bits read zero. Day overflow sets carry,
which software can clear through the day-high register.

The oscillator advances one second per 62,500,000 RCP cycles. Halt preserves the
fractional position; writes to counter registers preserve it. Mapper power reset
preserves clock, latch, and fractional state. Large advances are calculated
without iterating once per second. Access latency, subsecond write/halt behavior,
and unusual register values still need physical validation.

## Validation and remaining scope

`tests/cartridge/test_game_boy.cpp` covers capacity errors, ROM immutability,
unmapped windows, bank masks, both MBC1 modes, all MBC5 ROM banks, RAM enable,
independent RAM banks, nibble storage, mirroring, rumble wiring, and power reset.
`test_game_boy_clock.cpp` covers latching, rollover, carry, halt, masks, disabled
access, bulk/single advancement, and a maximum-width cycle advance.

MBC1 multicart wiring, MMM01, HuC1/HuC3, MBC6/MBC7, TAMA5, non-power-of-two ROM
layouts, and other boards are not selectable. Physical pull-up values, uncommon
register aliases, timerless board variants, and released-game compatibility also
remain unverified. Unsupported layouts must not be treated as a supported mapper
without an explicit configuration decision. These limits remain under #30,
#33, and #38.
