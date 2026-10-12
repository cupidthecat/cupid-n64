# Windows playback

The window thread handles input, menus, presentation and session commands. An
execution worker advances the console using latched controller input for a
complete video field. An audio thread owns the WASAPI endpoint and its COM
rendering interfaces.

Session commands request an interrupt between console execution intervals and
wait for the worker before changing the session, loading or saving a state,
opening debugger tools, or shutting down. Painting uses an owned image while
execution continues. Debugger execution uses the synchronous
path so stop, step and memory operations retain their existing boundaries.

Direct3D 11 uploads and scales the image into a flip-model swap chain. Scaling,
aspect ratio, fullscreen and screenshots retain the existing frame dimensions.
When GPU presentation fails, the frontend falls back to software presentation.
This presentation backend is separate from the Vulkan hardware renderer that
executes RDP graphics.

The hardware renderer reserves separate command-pool slots for its command queue,
serialized caller and readback worker. Renderer entry points bind the calling
thread to its slot when ownership transfers. State operations retain their GPU
fences before reading or replacing renderer state.

WASAPI uses shared-mode event-driven playback and a bounded stereo PCM queue.
The producer waits when the configured latency budget is full. Endpoint callbacks
request recovery after a device change; playback does not periodically enumerate
devices. The output menu reads a cached device list. Older saved device selections
are resolved to their endpoint IDs. Pause, seek and state changes clear pending
host audio without changing emulated AI timing.

## Reproducible replay

Windows accepts these additional command-line options with `--frames N`:

| Option | Purpose |
| --- | --- |
| `--input-replay FILE` | Supply player-one buttons and calibrated stick coordinates. |
| `--field-trace FILE` | Buffer field observations and host timings, then write CSV and JSON. |
| `--capture-fields FILE` | Capture the listed fields using the `--capture FILE` prefix. |

A replay file contains sorted, nonoverlapping ranges with five integers:

```text
first_field last_field buttons x y
```

The first field is inclusive and the last is exclusive. Inputs apply before
advancing from that field count. Unlisted ranges are neutral. Buttons are the N64
16-bit mask; stick coordinates are signed calibrated controller bytes from -128
through 127. Other player ports remain neutral during replay. Use fresh copied
media and saves when comparing runs. A field-indexed replay must be converted
explicitly if its original inputs were indexed by controller polls.

The capture list contains positive field numbers within `--frames N`. Captures
are binary RGB PPM files named `PREFIX-FIELD.ppm`; the final `--capture` image is
also retained. Closing a replay early returns status 2 and writes the available
trace with `complete: false`. Errors return status 1.

The trace records guest clocks, CPU PC, generated stereo-sample count, audio and
GPU availability, and host timing per field. `advance_us` measures the worker's
execution intervals; `paint_us` measures window painting; `wait_us` measures the
gap from worker completion to observation. `audio_wait_us` measures producer
buffer waits included in advance time. Painting and execution can overlap, so
their durations must not be added as a decomposition of total field time.

`worker_cpu_us` records the worker's kernel and user CPU time. Windows accounts
that time in discrete quanta, so compare sums across field windows rather than
individual fields. `worker_processor` samples the logical processor at interval
completion. `renderer_us` measures command submission, register updates, scanout,
readback and synchronization calls, including their CPU work and waits. It is also
included in execution time when those calls run on the worker.

Clock, PC, sample-count and image comparisons check the selected observations.
They do not establish equality of every machine register or RAM byte. Audio
availability also does not prove the absence of audible underruns.

## Performance acceptance

Issue #175 requires repeated visible-window runs with normal audio through boot,
menus, transitions and demanding gameplay. Check rolling field windows against
guest-clock duration rather than relying on the average or window-title counter.
The existing route excludes the mountain summit and does not establish performance
on every machine. Keep the performance issue open until its full criteria pass.

Worker, presenter and audio-backend regressions must pass alongside the existing
core, state and required hardware-renderer tests. Hosted checks must pass at the
exact PR head before merging. Private media, input scripts, captures and raw
timings belong outside the tracked source tree.
