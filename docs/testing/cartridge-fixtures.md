# Cartridge fixture corrections

The pinned cartridge has errors in its triangle oracle and CPU/RDP clock
measurement. CI applies two patches under `tests/cartridge/fixtures` before
building the default and extended ROMs. The patches retain all test names,
geometry inputs, enabled categories, and the clock test's tolerance of 20.

`triangle-fixtures.patch` converts ARGB host colors to RGBA command words and
converts RGBA32 framebuffer reads back to the host color type. Its CPU oracle
uses the eight staggered coverage samples, signed edge arithmetic, and the
first-sample rule selected by the fixture's disabled antialiasing mode. The
[triangle fixture guide](rdp-triangle-fixtures.md) describes these rules and
the literal command tests that check them.

`clock-sampling.patch` reads Count and DPC_CLOCK in one aligned instruction
pair. The initial sample and every later poll call the same helper. It compares
the DPC difference with four thirds of the actual elapsed Count ticks. The
original test could count a cold instruction-cache refill between its initial
reads and assumed polling stopped at exactly 100,000 ticks. See the
[clock sampling analysis](cartridge-build-layout.md#cpurdp-clock-sampling).

## Preparing source

Follow the checkout and build commands in [hardware validation](../testing.md).
The preparation entry point is:

```sh
python tools/ci/cartridge.py .work/nemu64-test \
  --report .work/test-fixture-corrections.json
```

Use a fresh, clean checkout of
`9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c` with LF line endings. The script
checks the source revision, each original file digest, the patch digests, and
every patch hunk before applying changes. It checks the resulting file digests
against `manifest.json` and records the unchanged commit and index alongside
the modified working-source digest. A dirty checkout, an unexpected file, or
an existing report stops preparation.

The manifest identifies four changed files: `src/graphics/color.rs`,
`src/rdp/rdp_assembler.rs`, `src/tests/rdp/filled_triangle.rs`, and
`src/tests/rsp_timing/mod.rs`. The upstream MIT license accompanies the patches.
The original Git objects remain available in the test checkout.

## Recording results

Keep the preparation report with the resulting ELF and ROM hashes. The base
revision alone no longer identifies the prepared source. CI's
`accuracy-test-inputs` artifact includes the report, patches, manifest, changed
Rust files, toolchain record, build configuration, and both ELF/ROM pairs.
The boot firmware remains a separate private input.

`tools/ci/validate.py --test-source .work/nemu64-test` records the effective
source and checks it again after the run. Strict and sanitizer jobs execute
every enabled category and propagate failed assertions. Preparing a corrected
cartridge is a source operation; it does not establish that the emulator
passes its tests.

Preserve earlier failure records with their original input hashes. A result
from a corrected cartridge is evidence for that cartridge, and comparison
with an older run must account for both the fixture changes and its new
instruction/data layout.
