# Render-worker lifetime

RDP drawing and VI scanout use the range helpers in `src/tasks/`. Each calling
thread can keep up to three workers and do the remaining work itself. Separate
emulation threads own separate pools. A nested range job runs on the worker
that called it, avoiding another pool or a wait on the current job.

## Completing a job

A job publishes its immutable bounds and callback with a release store to the
generation counter. Workers acquire that generation before reading the job.
The caller waits for every worker's completion before returning or propagating
an exception. A callback can therefore refer to its caller's stack for the
duration of the synchronous call.

Worker creation can stop after a resource failure. The pool uses the workers
that were created; if none are available, the caller executes the whole range.
Job submission does not allocate memory. RDP tasks record memory-bank effects
in separate summaries and merge them in row order after all drawing completes.
See [synchronous row execution](../hardware/rdp-color.md#synchronous-row-execution)
for the memory conditions that permit parallel drawing.

## Ending an owner thread

The desktop emulation loop and command-line runner hold a
`tasks::ParallelRangesScope`. Its destructor calls
`shutdown_parallel_ranges()` while normal C++ code is still running. This
signals the workers, joins them, and releases their state. A subsequent job on
the same thread can create another pool. Shutdown requested from inside a job
does nothing, so a worker cannot join itself or interrupt its current caller.

Core callers that omit the scope still have a fallback at thread exit. The
thread-local owner signals its workers and detaches their handles without
waiting. Each worker owns a reference to the shared job state until it returns.
Completed jobs no longer use the saved callback context. On Windows, this
avoids joining exiting threads from a thread-local destructor while the loader
lock is held. Normal frontend shutdown uses the explicit join path.
Microsoft's [DLL guidance](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices)
describes the dependency between thread-detach cleanup and the loader lock.

## Regression coverage

`tests/tasks/test_parallel_ranges.cpp` checks complete row coverage, signed
range boundaries, exception propagation after completion, nested work, and
concurrent callers. Native-thread exit is tested separately from `std::async`,
which can reuse operating-system threads. The explicit-shutdown case observes
worker thread-local destruction before returning, then starts another job.

RDP and VI regression tests compare parallel output with serial execution.
Timed desktop runs also require a successful process exit after saving and
capturing the final window; a screenshot alone cannot detect a shutdown hang.
