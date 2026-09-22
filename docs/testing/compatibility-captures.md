# Cartridge output captures

`cupid-compatibility-capture` boots a locally supplied cartridge through the PIF
and records video fields and audio samples. It uses the same hardware options as
the command-line runner. For example:

```sh
build/cupid-compatibility-capture captures/run-001 360 cartridge.z64 \
  --pif pif.ntsc.rom --save eeprom4k --controller 1:gamepad \
  --max-instructions 1000000000
```

Create the parent `captures` directory first. The output directory must be new;
the tool refuses to overwrite an existing capture. Supply the cartridge's save
type and controller configuration explicitly. See [hardware configuration](../hardware/configuration.md)
for accepted options. Persistent storage options load and flush files just as
they do in the runner; use copies when comparing runs from the same saved state.

The capture contains:

- `run.txt`: hardware configuration, input fingerprints, execution limits,
  final CPU and DP state, sample count, and whether the field limit was reached.
- `fields.csv`: one record per video callback, with field dimensions, parity,
  interlace state, CPU cycles, instruction count, PC, DP status, cumulative audio
  counts and hash, and a hash of the field's RGBA pixels.
- `last-field.ppm`: the final video field as RGB pixels. Interlaced output is a
  single field, not a pair of woven fields.
- `audio.s16be`: signed 16-bit stereo samples, left then right, most significant
  byte first. The cartridge can change the DAC rate; this raw stream has no
  fixed playback-rate declaration. Field records provide cumulative sample
  counts, not individual sample timestamps.

Fingerprints use FNV-1a 64-bit. RGBA and audio values are hashed most significant
byte first, so host byte order does not affect comparisons. Cartridge fingerprints
cover normalized big-endian ROM bytes; the PIF fingerprint covers its 1984-byte
code area. These hashes detect output differences but are not cryptographic
input identifiers. Record the Git revision, full command, SHA-256 of each original
input file, and initial storage files alongside a compatibility result.

Exit status zero means the requested field count was reached without a CPU freeze
or PIF boot failure. It does not establish correct rendering, sound, or gameplay.
Blank fields count, and a stalled game can keep producing video callbacks. The
tool supplies no button presses, reset sequence, or gameplay script. Compare
captures at the same field count and configuration, inspect the image and audio,
and record scenario-specific expectations before marking a cartridge compatible.

With `CUPID_TEST_ROM` and `CUPID_PIF_ROM` configured, CTest checks repeatability,
Unicode output paths, instruction-limit failure, preservation of existing output,
and unchanged cartridge and firmware files. This validates the recorder, not the
cartridge's hardware behavior.
