# Audio interface timing

AI consumes a 32-bit stereo sample at each active DAC boundary: signed 16-bit
left in the high halfword and signed 16-bit right in the low halfword. The
sample period is `AI_DACRATE + 1` video clocks, using the NTSC or PAL oscillator.
Writes retain the low 14 divider bits. An explicit zero selects one video clock;
it does not select a fallback rate.

The period is latched for the interval already in progress. A divider write
changes the next interval, so shrinking the divider cannot consume several
queued samples at the write, and growing it cannot postpone the pending sample.
When several writes occur within one interval, the latest value selects the
following period. Writes before the first elapsed cycle configure the first
interval. Rate writes made by an audio callback select the period following
that callback's sample.

The core's reset clock runs at 44,100 Hz. The first programmed rate takes over
at a sample boundary. Both clocks share an integer timing base, preserving
fractional phase through the first programmed rate and subsequent rate changes.
Reset discards active and pending divider state. These rules describe the core's
clock model; independently captured power-on and divider-write traces are still
needed to verify hardware phase precisely.

The oscillator continues while DMA is disabled or the FIFO is empty. Divider
changes latch at those idle boundaries too. Re-enabling DMA or enqueuing a buffer
joins the existing phase rather than restarting it. Idle intervals can advance
in bulk without overflowing the integer clock accumulator.

## FIFO and output

The two-entry FIFO retains addresses and lengths independently. Addresses align
to eight bytes, and lengths retain the low 18 bits with the low three cleared.
Writes to a full FIFO are ignored. Starting an empty FIFO raises the AI interrupt;
promoting the second buffer raises it again. Completing the final buffer does
not raise a new interrupt. Writing STATUS acknowledges it.

Each consumed sample advances the low 13 address bits by four. A carry from
that addition is applied to the upper address bits at the next sample, including
across a FIFO handoff. A zero-length buffer retires at a DAC boundary even with
DMA disabled. Reads outside STATUS mirror the active remaining length.

`Bus::audio_output` receives consumed stereo samples after their address and
length updates, FIFO retirement or handoff, and handoff interrupt. A callback
therefore sees the remaining length and available FIFO slot for that boundary.
It can acknowledge the handoff interrupt or enqueue a new buffer immediately;
those actions are not overwritten by unfinished sample processing. Delivery
waits until the enclosing device boundary, including SP work in CPU-driven
advances, has finished. A transfer started by the callback begins at that clock.
Retiring a zero-length buffer alone does not deliver a sample.

If a video callback queues audio exactly at a DAC boundary, the new buffer joins
the following interval. It cannot supply the sample already consumed at that
clock. Divider writes during output delivery at a DAC boundary select the next
period; writes at other output boundaries preserve the current interval.
Buffered CPU writes retain their device-stage order after the DAC latch and do
not use the output callback's rate-selection rule.

The scheduler stops at sample deadlines so callbacks observe the corresponding
DP clock. While video callbacks are registered, idle DAC boundaries also
constrain scheduler advances.
This prevents a video callback that starts audio from assigning its new buffer
to audio periods that elapsed before the callback. A rate change from that
callback preserves the pending audio deadline.

## Validation and limits

`tests/rcp/test_ai.cpp` covers FIFO state, interrupts, regional rates, disabled
DMA, address carry, and sample values. `test_ai_timing.cpp` covers shrinking and
growing dividers, successive writes, register masking, initial programming,
default-clock handoff, reset, idle phase, callback-driven rate changes, and large
advances. A mixed-device trace compares bulk and single-cycle CPU advances while
video callbacks change the rate, SP/SI DMA change sample memory, and RI refresh
is active.

`test_ai_output.cpp` checks callback-visible lengths and FIFO status, interrupt
acknowledgement at handoff, refilling a newly freed slot, and restarting an empty
FIFO. Refill tests compare bulk advances with single-cycle advances and verify
both channels of all six samples across three buffers.
The divider-handoff trace expects the interrupt on the outgoing buffer's last
sample callback, when promotion occurs.

`test_ai_conformance.cpp` adds independently specified raw RDRAM/register
fixtures with literal signed samples and RCP timestamps. The fixture inputs,
source documents, hashes, exact comparisons, and remaining limits are recorded
in [the audio conformance fixture notes](../testing/audio-conformance-fixtures.md).

The SDK's [osAiSetFrequency documentation](https://ultra64.ca/files/documentation/online-manuals/man/n64man/os/osAiSetFrequency.html)
describes selecting internal divisors and returning the resulting frequency.
The extreme divider tests here exercise register arithmetic; they do not establish
valid analog output at every possible register value.

BITRATE is stored but its serial-clock effects are not modeled. Empty-FIFO output
and analog decay are also outside the sample callback model. AI fetches do not
yet arbitrate shared RDRAM bandwidth. Hardware sample captures and those timing
paths remain part of issue #25.
