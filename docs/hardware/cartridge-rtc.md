# Cartridge real-time clock

The cartridge RTC is an optional Joybus device on channel four. It is independent
of `SaveType`, so it can coexist with EEPROM, SRAM, or FlashRAM. The default is
absent. A caller installs it with `bus.rtc.emplace(registers)` and removes it with
`bus.rtc.reset()`. `CartridgeRtc::Registers` contains 32 bytes; callers supply the
initial control, storage, and calendar bytes explicitly. `registers()` provides a
read-only view. Installing a new instance starts a fresh subsecond interval.

The core advances the clock using emulated time. It does not read the host clock,
choose a time zone, infer RTC presence from a ROM, or load/save an RTC file.
Cartridge configuration and persistence remain tracked under #33 and #34.

## Commands and registers

| Command | Minimum command/reply bytes | Reply |
| --- | --- | --- |
| `06` status | 1 / 3 | `00 10` followed by status |
| `07 bb` read | 2 / 9 | Eight bytes from block `bb`, then status |
| `08 bb` write | 10 / 1 | Status after the write |

Status is `00` while running and `80` while stopped. Short packets and absent
devices leave reply bytes untouched and set the PIF no-response flag. Extra
command bytes are ignored; extra reply bytes are zero. These commands do not
respond on the four controller channels. EEPROM commands retain their separate
status and busy interval.

Each block holds eight bytes. The current decoder uses the low two block bits.
Block zero contains the control word in big-endian order. Bit 2 stops the clock;
bits 8 and 9 independently protect blocks one and two from writes. A protected
write still returns status. Block zero remains writable. Other control bytes,
including calibration data, are stored and returned without interpretation.
Block one is ordinary retained storage. The fourth bank is retained raw storage
in the current model; no host timestamp is exposed there. Hardware captures are
still needed to establish reserved-bank and out-of-range address behavior.

Block two contains packed BCD seconds, minutes, hours, day of month, weekday,
month, year within the century, and centuries since 1900, in that order. The
hour byte has bit 7 set; weekdays run from 0 through 6 and months from 1 through
12. For example, `00 00 80 01 06 01 00 01` represents midnight on January 1, 2000.

The supported set-time sequence writes control `0004`, writes block two, and
writes control `0300` to resume with both data blocks protected. Calibration
bytes survive when the caller carries them through those control writes. The
command layout, control values, and date encoding are also exercised by
[libdragon's RTC driver](https://github.com/DragonMinded/libdragon/blob/e356bf3f56f7afbf7e5246329562f145965cfdfc/src/rtc.c).

## Clock and reset behavior

A second takes 62,500,000 RCP cycles, equivalent to 93,750,000 CPU cycles.
Partial seconds carry across ordinary advances. Stopping the clock prevents
calendar advancement. Every accepted control-block write restarts the subsecond
interval, including a write that leaves the clock running. Writes to other
blocks do not restart it. Write protection does not prevent the clock itself
from updating the calendar.

Clock ticks occur before SI command execution at a shared completion boundary.
A read at that boundary sees the new second. A run command completed through SI
starts a full second at completion, without consuming the DMA's elapsed time.
Console reset preserves presence, registers, stop state, and protection; it
restarts the subsecond interval. Replacing or removing the optional device
discards its old state.

Calendar carries use month lengths and a four-year leap cycle. The tests compare
every month end from 1996 through 2099 against an independent calendar calculation,
including February 2000 and the century carry. Behavior at later century
exceptions, malformed BCD, oscillator calibration, battery loss, and physical
write latency still needs hardware validation. Writes currently take effect
when the Joybus command executes. Software compatibility delays are not treated
as measured hardware deadlines.

## Validation

`tests/cartridge/test_rtc.cpp` covers presence with every save type, controller
channel isolation, stop/set/run, calibration retention, independent write locks,
all 256 block addresses, bank isolation, malformed and padded packets, calendar
carries, fragmented advances, CPU/RCP conversion, console reset, and EEPROM
busy-state independence. SI tests configure requests by CPU store or write DMA,
then execute through read DMA. They cover reads at a
shared tick boundary under bulk and single-cycle CPU/RCP advances. Separate
tests check the first complete second after an SI run command.

These tests validate the implemented model. SI deadlines still use the current
estimates described in [Joybus packets](joybus.md); RTC transaction captures and
a cartridge-level compatibility case remain unfinished under #28, #30, and #38.
