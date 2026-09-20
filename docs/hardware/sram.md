# Cartridge SRAM

`Bus::set_save_type(SaveType::Sram)` creates a zero-filled 32 KiB save image if
the SRAM array is empty. Existing contents and capacity are retained. The
current host interface exposes the image as `Bus::sram`; banked configurations
are supplied by resizing or loading that array before accessing the cartridge.
There is no separate SRAM chip selector or command-line capacity option yet.

## Address selection

SRAM responds in cartridge addresses `0x08000000` through `0x0fffffff` while
the selected save type is SRAM. The tested capacities are:

| Image capacity | Address mapping |
| --- | --- |
| 32 KiB | One window mirrored every `0x8000` bytes throughout the save region. |
| 96 KiB | Three 32 KiB banks starting at `0x08000000`, `0x08040000`, and `0x08080000`. |
| 128 KiB | Four 32 KiB banks, adding `0x080c0000` to the preceding map. |

Banked images respond only in the first 32 KiB of each 256 KiB bank window.
The remaining addresses and absent banks do not respond. This table records
the configurations covered by regressions, not a claim that arbitrary save
image lengths correspond to supported physical chips.

An address phase selects a memory window and starts a sequential halfword
transfer. Reaching the end of that window does not wrap or enter the next bank.
A read then retains the last halfword on the PI latch; a write updates the latch
but leaves storage unchanged. A new address phase can select a mirror or another
bank. DMA generates those phases at the programmed domain-2 page boundaries,
including when the transfer crosses an unmapped gap. Crossing a 128-byte DMA
buffer alone does not select another address.

## CPU access, DMA, and reset

CPU accesses use two cartridge halfword beats. Byte and halfword loads select
their lane from that assembled word. Subword stores still drive a full word,
with the supplied value shifted into its lane and the other lanes zero; SRAM
has no CPU byte-enable mask. At the selected window's final halfword, the second
store beat is ignored and the second read beat retains the first beat's value.

CPU stores update the save array and hold the full word in the PI latch while
I/O is busy. Another store during that interval is ignored. A cartridge read
during the interval returns the held word and releases the latch. The existing
140-RCP-cycle I/O deadline also releases it. Console reset clears the PI's busy
and transaction state while retaining the save type, image size, and all SRAM
bytes. Selecting SRAM again does not erase an existing image.

Both DMA directions use the same selected windows. The current DMA model copies
payloads at transfer start and delays completion/interrupt delivery; it does not
yet expose beat-by-beat progress. See [PI timing](peripheral-interface.md) and
issue #26. SRAM has no EEPROM-style program-busy interval.

## Validation and remaining work

`tests/cartridge/test_sram.cpp` covers all three capacities. It writes and reads
every word in every bank, checks all 4,096 possible 32 KiB windows across the
save region, verifies subword lanes and window tails, and rejects writes to
bank gaps and absent banks. DMA tests cover all 16 page sizes, both directions,
page/window/bank boundaries, full-bank round trips, and surrounding RDRAM guard
bytes. Whole-image comparisons check bank isolation and reset preservation.
CPU write-busy tests compare bulk and single-cycle CPU/RCP advances.

The mapping oracle describes each selected page as a responding prefix and an
open-bus tail. Separate mutation checks confirmed that the regressions detect
bank aliasing, missing mirrors, and transfers spilling into the next bank.
Those checks use temporary altered builds; the production decoder already
passes this coverage and was not changed to satisfy it.

The complete cartridge suite remains part of validation, but it does not supply
SRAM-specific save/readback tests. Game-level persistence across fresh machine
instances is still unimplemented under #34. Cartridge configuration (#33),
shared-memory arbitration, and timed PI payload progress remain unfinished;
these tests do not establish those behaviors. Issue #31 stays open for the
remaining save-hardware and persistence requirements.
