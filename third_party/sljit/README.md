# SLJIT

This directory contains the SLJIT 0.95 source and its redistribution license.
`sljit_src/` is vendored without local source edits. The project builds
`sljitLir.c`, which selects the host backend, with
`SLJIT_WX_EXECUTABLE_ALLOCATOR=1`.

The RSP integration is in `src/rsp/native.cpp` and `cmake/rsp-native.cmake`.
Keep this license and the source copyright notices when updating the dependency.
Binary distributions that enable native RSP execution must include `LICENSE`.
