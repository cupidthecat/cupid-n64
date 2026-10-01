# Optimized build source validation

Date: 2026-10-01. Preceding revision: `53b4f756288d0b9e3713d9618c1e134aeb69e139`.

Profile packages hash implementation files throughout the subsystem folders.
CMake records the sources attached to the core and host library targets,
including the enabled native RSP emitters. Package creation checks that this
membership still matches the training snapshot. Source identity schema 2
requires fresh training for older packages with incomplete file identities.
See [profile-guided builds](profile-guided-builds.md) and #86.

## Regression checks

Three focused checks reproduce the original omission: changes to nested RSP
implementation files and additions of nested CPU code leave the old identity
unchanged, while a snapshot containing an enabled nested emitter fails its
membership check. All pass with the corrected identity.

Further checks cover removed nested files, missing or duplicate membership,
unknown source paths, membership changes after training, malformed source
records, and the membership path recorded by a nested CMake build. All 32
profile-package checks and all 75 tooling checks pass.

Actual Clang 22.1.8 instrumentation configuration records and hashes
`src/rsp/native/clip.cpp`, `plan.cpp`, and `vector.cpp`. Its complete hardware
run passes all 1,576 checks. Game profile collection is a separate validation
step and does not establish gameplay speed.

## Complete ordinary validation

Clang 21.1.5 Release with interprocedural optimization passes all 1,576 hardware
and host regressions, 4,637 prepared default cartridge cases, 6,273 prepared
extended cases, default cold and warm boots, and stepped/batched comparisons.
All nine CTest groups, 75 tooling checks, and clang-format 22.1.0 pass. Source,
prepared test-source, and cartridge inputs remain verified.

The 547-file validation snapshot has SHA-256
`f8ac59bbceb84bc329bc1b78a1cfa11541437651b267d60fa02aff4caf5f85c7`.
The retained local report ID is `1c6b9c165f9e4853b2c2d3a93271f4db`.
The changes affect profile binding; the emulation implementation is the one
covered by the [four-configuration clipping matrix](rsp-clipping-results.md).
Prepared-image corrections and original-image acceptance remain documented in
[cartridge fixtures](cartridge-fixtures.md).
