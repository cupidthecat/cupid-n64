# Desktop audio output

The optional desktop audio module accepts the emulator host resampler's fixed
48 kHz, signed 16-bit, interleaved stereo PCM. It does not alter emulation
speed or perform timing feedback into the core.

Enable it with `-DCUPID_DESKTOP=ON`. Headless builds leave this option off and
do not configure or link SDL. The desktop module first accepts an installed
SDL3 CMake package at exactly version 3.4.16. When one is not available, CMake fetches the official SDL
3.4.16 source archive:

- `https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz`
- SHA-256 `7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68`

The module links the SDL-provided `SDL3::SDL3` target, which may resolve to a
shared or static SDL library according to the installed package or SDL build
configuration.

## AudioOutput

`cupid::desktop::AudioOutput` opens the system default playback device with
`SDL_OpenAudioDeviceStream`. The application side of that stream remains
`SDL_AUDIO_S16`, two channels, 48 kHz; SDL converts to the host device format
when necessary.

`submit()` accepts complete stereo frames. The input queue is capped at 4,800
frames, exactly 100 ms at 48 kHz. Frames beyond that cap are not queued and are
counted in `AudioStatus::dropped_frames`. Queue reporting uses SDL's input-byte
count, so `queued_frames` remains meaningful even when the host device uses a
different output format.

Playback waits for a 960-frame, 20 ms prefill. If a running stream drains before
the next submission, the wrapper records an underrun, pauses the device, and
returns to the same prefill requirement before playback resumes. This limits
recovery to queueing policy; it never changes the core or resampler rate.

`set_running(false)` pauses playback and clears queued PCM. `clear()` also
pauses an active stream before discarding queued PCM, which is suitable for
reset/discontinuity handling. `close()` destroys the stream and releases the
audio subsystem reference acquired by `open()`.

`set_volume(gain, mute)` stores the logical non-negative gain and sets the SDL
stream gain to either that value or zero. Toggling mute therefore does not
multiply or accumulate previous volume changes.

If SDL reports that the stream no longer has a bound device, submissions and
run-state changes fail with an actionable error. Calling `open()` again
destroys the stale stream and opens the current system default device. The
requested running state is then recovered through normal prefill.

## Status and validation

`AudioStatus` reports whether a stream is open, whether its device is
available, whether playback has actually resumed, logical gain/mute state,
queued frames, lifetime dropped frames and underruns, and the last actionable
error.

`cupid-desktop-audio-tests` uses SDL's dummy playback driver for device
lifecycle, prefill/underrun recovery, queue capacity, volume/mute, clear/pause,
reopen, and open-failure coverage. A native Windows device-open smoke can be
run separately with `CUPID_AUDIO_NATIVE_SMOKE=1`; it establishes that the
default device opens, not that audible output was heard.
