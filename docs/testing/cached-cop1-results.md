# Cached COP1 validation (2026-09-26)

Cached CPU slices accept MOV, ABS, NEG, SQRT, BC1F, and BC1T when their live
operands and control state permit execution without a trap. The
[CPU timing guide](../hardware/cpu-timing.md) describes admission checks,
instruction costs, register aliases, and fallbacks.

Seven new regressions cover unary results, full-width MOV payloads, FCSR causes,
paired registers, rounding, exception destinations, cycle limits, branch delay
slots, RSP execution, SP DMA, timer interrupts, and audio callback timestamps.
Five checks fail when linked against the preceding core at `04de8a4` because
that core cannot admit the new operations. State and timing comparisons use
ordinary instruction stepping as the execution-path oracle.

Strict Windows Clang 21.1.5 Release and ASan/UBSan runs passed the same source
snapshot. Each passed all nine CTest groups, 1,462 hardware and host regressions,
64 validation-tool tests, and the clang-format 22.1.0 check. The prepared default
cartridge passed 4,637 cases; the prepared extended cartridge passed 6,273,
including 1,604 timing cases. Stepped and batched cartridge results matched.
No sanitizer diagnostics were reported. These images retain the documented
[fixture corrections](cartridge-fixtures.md).

Local reports are retained at:

- `.work/build-gameplay/validation/df0fe60713924b1ba1d91fcb3fd213b0/report.json`
- `.work/build-gameplay-sanitize/validation/579fc9411c364383874d2cf62d4da437/report.json`

A local controller replay pressed Start at field 1,200, then A at fields 1,500
and 1,740, releasing each button after 20 fields. Super Mario 64 (USA) reached
the castle introduction and Mario's exit from the pipe at field 4,200. Both
cores completed 4,181,999,616 CPU steps and emitted 2,229,503 stereo sample pairs.
All 4,200 video records, the audio stream, retained images, and final machine
reports matched byte for byte. The title, file menu, castle roof, and final
Mario image were inspected. Inputs, the local replay source, and captures remain
under `.work/`; commercial cartridge and firmware bytes are not published.

In a separate 600-field replay, cached retirement increased from 134,021,887 to
134,182,698 instructions; idle retirement and all video/audio output were
unchanged. Two preceding-core runs took 15.690 and 15.638 seconds, and two revised
runs took 15.155 and 15.405 seconds. Other validation was active during these
measurements, so they do not establish a sustained performance improvement.
Full-speed gameplay, audible playback, level completion, and save/reload remain
unverified under issues #48 and #47.

This results record and its index link were added after validation; implementation
and regression files were unchanged.
