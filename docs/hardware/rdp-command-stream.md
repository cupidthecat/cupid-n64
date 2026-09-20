# RDP command streams

DPC_START and DPC_END select a DMA range. The command processor consumes the
transferred words as one stream, even when successive ranges are not adjacent.
Writing a new START changes where the next DMA reads; it does not discard an
incomplete command already in the FIFO.

A command executes only after all its words arrive. Triangle packets contain
between four and 22 doublewords, and texture rectangles contain two. Opcode-like
bits inside a packet remain parameters. In particular, a parameter word whose
high byte is `0x29` must not raise the SyncFull interrupt.

Buffered words also survive a change between RDRAM and XBUS DMEM sources. DMEM
addresses wrap at 4 KiB while DPC_CURRENT continues through the selected DMA
range. Reset discards incomplete commands.

`tests/rdp/test_command_fifo.cpp` splits every multiword opcode at every
doubleword boundary, submits the tail from a different address, and checks that
payload words cannot issue interrupts. The same cases switch the tail to DMEM
across its address wrap. A separate reset test verifies that a fresh SyncFull
does not become part of an abandoned packet.

These tests cover stream assembly and interrupt decoding. They do not establish
command DMA timing, FIFO capacity, pipeline overlap, or flush timing; those remain
part of the DP synchronization work.
