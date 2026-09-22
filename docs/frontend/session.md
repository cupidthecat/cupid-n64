# Host session and output ownership

`cupid::host::Session` owns one emulated `System` on a worker thread. Window,
input-device, and audio-device APIs stay on the desktop thread. The UI sends
bounded commands for load, pause, reset, and stop, and reads snapshots through
a mutex. Command identifiers allow callers to wait for completion without
reading mutable core state.

The command queue holds at most 32 pending requests. Emulation yields between
batches bounded by 65,536 instructions or 93,750 CPU cycles. Pacing uses the
93.75 MHz CPU clock and a condition variable, so a pause or stop can interrupt
a pacing wait. If the host falls behind, its old wall-clock deadline is
discarded; every guest instruction and hardware event still executes.

Loading first creates and validates a candidate machine. Storage names use
the bytes loaded into that machine. The old machine is flushed before the
candidate becomes active, and the candidate reloads persistent storage after
that flush so a cartridge reload sees the latest saved bytes. A failed load
or flush retains the old machine in a paused state.

A stop flushes before destroying the machine. Failed flushes leave it
available for retry. Explicit discard is a separate command. Session
destruction stops and joins the worker before destroying any state used by
core output callbacks. It attempts a final flush and reports shutdown errors.

Video delivery owns its pixel vectors. The session retains only the latest
composed display frame, so slow presentation cannot accumulate a frame queue.
The [video composer](video.md) handles progressive output, interlace history,
and blank fields independently of core VI timing.

The core's timed audio callback carries a stereo sample, an RCP timestamp,
the active rational sample rate, and whether the sample came from DMA. Idle
DAC intervals emit silence through this callback; the older DMA-only callback
retains its original behavior. Rate changes are reported at their actual
sample boundary. Reset changes the output generation so callbacks queued by
the previous generation cannot escape after reset.

The host resampler produces 48 kHz signed 16-bit stereo from the sample
timeline. It retains fractional time across sample-rate changes and bounds
large discontinuities. Its output queue holds 4,800 stereo frames and drops
the oldest complete frames when capacity is exceeded. The SDL audio layer
has its own bounded device queue and reports underruns and discarded frames.
Neither queue feeds timing changes back into the hardware core.

Pause, reset, load, and stop advance an output epoch and clear queued audio.
The UI clears its device queue when it observes a new epoch. Reset and pause
can retain the last video image; stopping or replacing the machine publishes
an explicit blank frame and discards incompatible interlace history.

`tests/host/test_session.cpp`, `test_audio.cpp`, and `test_video.cpp` exercise
session commands, worker ownership, bounded output, persistence failures,
time interpolation, and field composition. `tests/rcp/test_audio_output.cpp`
checks callback timestamps and reset/rate-change ordering against the core.
