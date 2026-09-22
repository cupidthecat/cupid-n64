# Audio conformance fixtures

These fixtures exercise the Audio Interface with literal RDRAM words and raw
register writes. They are deterministic specification fixtures, not recordings
from a physical console. No expected sample or timestamp is generated from the
implementation under test.

The register rules come from the Nintendo/SGI `rcp.h` hardware definitions:
AI DMA addresses are eight-byte aligned, lengths use 18 bits with the bottom
three ignored, `AI_DACRATE + 1` selects the sample period from the video clock,
and `AI_BITRATE` is a four-bit audio-bus half-clock divider. The N64 introductory
manual describes the AI buffer as signed 16-bit stereo waveform data. The SDK
`osAiSetNextBuffer` documentation supplies the eight-byte buffer alignment and
length requirements.

- `https://ultra64.ca/files/documentation/online-manuals/man/header/rcp.htm`
- `https://ultra64.ca/files/documentation/online-manuals/man/kantan/step2/3-1.html`
- `https://ultra64.ca/files/documentation/online-manuals/man/n64man/os/osAiSetNextBuffer.html`

## Raw FIFO fixture

The FIFO fixture writes the following RDRAM words. Its provenance hash is the
SHA-256 of each address followed by its word value, both encoded as four-byte
big-endian integers in table order: `71b084b2eb041fd59f8f42809297ebcbc5d7143214ff8aac3a9fd03d743e86c5`.

| Address | Word | Signed stereo sample |
| --- | --- | --- |
| `0x00002000` | `0x80017fff` | `(-32767, 32767)` |
| `0x00002004` | `0xffff0001` | `(-1, 1)` |
| `0x00003000` | `0x7fff8000` | `(32767, -32768)` |
| `0x00003004` | `0x1234fedc` | `(4660, -292)` |
| `0x00004000` | `0x0001fffe` | `(1, -2)` |
| `0x00004004` | `0x7ffe8002` | `(32766, -32766)` |

The raw writes set DACRATE to `131`, BITRATE to `0xfffffff1`, enable DMA,
queue address `0x2007` with length `0x0f`, then queue `0x3007` with the same
length. The hardware masks make those buffers `(0x2000, 8)` and `(0x3000, 8)`.
A third write while both FIFO entries are occupied is ignored. After one empty
DAC boundary, `(0x4007, 0x0f)` is queued.

For NTSC the literal `(clock, left, right)` comparisons are `(170, -32767,
32767)`, `(339, -1, 1)`, `(509, 32767, -32768)`, `(678, 4660, -292)`,
`(1017, 1, -2)`, and `(1187, 32766, -32766)`. There is no callback at clock
`848`; this checks underflow without restarting DAC phase. The test also compares
remaining length, BUSY/FULL state, and the AI interrupt at every delivered word.

## Bit-clock and DAC-boundary fixtures

BITRATE controls serial clocking; it does not change the 16-bit PCM word format.
The valid BITRATE values `1`, `7`, and `15`, plus `0xffffffff` to exercise the
four-bit write mask, therefore all produce the same two signed samples from
`0x00ff8001, 0x7f00ff7f`: `(255, -32767)` and `(32512, -129)`. The address/value
pair hash for those two input words is
`a241597832587e81176fc790a12f2d2d8d1cb5ee8c802cabe8273ab61712f162`.

The timestamp table uses the documented NTSC clock `48,681,812 Hz`, PAL clock
`49,656,530 Hz`, and the RCP `62,500,000 Hz` timing base. It compares literal
RCP clocks rather than recomputing them through `System::video_frequency()`.

| Region | DACRATE | BITRATE | First sample | Second sample |
| --- | ---: | ---: | ---: | ---: |
| NTSC | 131 | 1 | 170 | 339 |
| PAL | 131 | 1 | 167 | 333 |
| NTSC | 1103 | 15 | 1418 | 2835 |
| PAL | 1103 | 15 | 1390 | 2780 |
| NTSC | 16383 | 15 | 21035 | 42070 |
| PAL | 16383 | 15 | 20622 | 41244 |

## Coincident output fixture

The simultaneous-output fixture makes the first VI callback and an empty AI DAC
edge land on RCP clock `2183`. The callback queues two words and changes DACRATE
from `1699` to `131`. The elapsed empty edge cannot consume the new buffer, so
the exact sample callbacks are `(2353, 4369, -4370)` and `(2522, -32768,
32767)`. Bulk and single-cycle advances must match. The two address/value pairs
hash to `1f9f00c39eddba81fecb779f6944510a30faddecf2aad10e07653c98c1c78e86`.

## Coverage limits

Mutation checks were run against the fixture slice before validation. Swapping
the two arguments passed to the stereo output callback made the raw FIFO,
BITRATE, and coincident-output fixtures fail on their literal channel values.
Removing the rate latch performed during a coincident output boundary made the
coincident-output fixture report zero samples where two were required by clock
`2522`. Both mutations were reverted before the validation runs.

These fixtures establish digital word ordering, signed channel interpretation,
register truncation, FIFO handoff/underflow behavior, supported-region DAC
boundaries, and one coincident output boundary. They do not establish analog DAC
behavior, BITRATE zero or invalid divider combinations, physical-console phase
after long runs, or RDRAM arbitration during an AI fetch. Those need separate
hardware measurements and shared-memory timing work.
