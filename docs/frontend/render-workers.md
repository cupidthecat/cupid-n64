# Render-worker lifetime

RDP drawing and VI scanout use the range helpers in `src/tasks/`. Each calling
thread can keep up to three workers, execute its own range, and finish ranges
that workers have not claimed. Separate emulation threads own separate pools.
A nested range job runs on its current thread and keeps the enclosing range
index, avoiding another pool or a wait on the current job.

## Completing a job

A job publishes its bounds and callback before making the new generation
available for claiming. Each worker must atomically claim its range for that
generation before reading the job fields. After finishing its own range, the
caller can claim and execute ranges that workers have not started.

The caller waits for every callback to finish before returning or propagating
an exception. It does not need to wait for a worker that missed a job already
finished by the caller. A late worker sees either an existing claim or a
different generation and leaves the saved callback context untouched. This
keeps stack references valid for the synchronous call and allows the job
storage to be reused after completion.

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
Repeated short jobs also destroy each callback's heap storage immediately
after completion to check that later work cannot retain that context.

RDP and VI regression tests compare parallel output with serial execution.
Timed desktop runs also require a successful process exit after saving and
capturing the final window; a screenshot alone cannot detect a shutdown hang.
