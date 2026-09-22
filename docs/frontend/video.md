# Desktop video composition

The desktop frontend receives owned `VideoField` values from the core and uses
`cupid::host::FrameComposer` to turn them into complete display frames. This is
host presentation logic only; it does not change VI timing, filtering, RDP
rendering, or the field callback schedule.

`Bus::scan_video()` currently produces 640-pixel-wide fields with a height of
240 for NTSC or 288 for PAL. `FrameComposer` therefore rejects nonblank input
above 640 by 288 before allocating output storage. It also rejects zero-sized
partial geometry, parity values above one, and any field whose pixel count does
not equal `width * height`. A default `VideoField{}` or the equivalent explicit
zero-by-zero noninterlaced field is the blank signal; it returns an empty
`DisplayFrame` and clears interlace history. Malformed input returns
`std::nullopt` without changing the last valid field.

Progressive fields are passed through exactly, including their packed RGBA8888
pixel words. Interlaced fields produce a display frame with twice the field
height. The first field, or a field whose neighboring parity is unavailable, is
bobbed by duplicating every source row. Two consecutive fields with opposite
parity and matching geometry are woven: parity zero occupies even display rows
and parity one occupies odd display rows. If the same parity arrives twice, the
newer field replaces the old history and is bobbed; a following opposite field
weaves against that newer neighbor. A geometry or progressive/interlaced mode
change discards incompatible history. `reset()` clears history explicitly.

The SDL adapter can use `letterbox_4_3()` to calculate an integer destination
rectangle inside the current window. The helper is independent of SDL, centers
the largest rounded 4:3 rectangle that fits the supplied dimensions, and
returns an empty rectangle when either window dimension is zero. Very small
windows use the closest nonzero integer rectangle that still fits.

`tests/host/test_video.cpp` checks literal RGBA words and row order for
progressive pass-through, first-field bob, both parity orders, repeated parity,
geometry and mode changes, explicit blank/reset behavior, malformed input,
maximum core geometry, and small/wide/tall letterbox destinations. These tests
cover deterministic host composition only; they do not claim desktop-driver,
monitor, or gameplay validation.
