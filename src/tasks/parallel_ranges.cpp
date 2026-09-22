#include "parallel_ranges.hpp"

#include <array>
#include <atomic>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <system_error>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#endif

namespace cupid::tasks {
namespace {

thread_local bool inside_task = false;
thread_local unsigned task_index = 0;

template <typename T> T wait_for_change(std::atomic<T>& value, T observed) {
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    // Adjacent row jobs often finish or arrive before a sleeping thread can be
    // rescheduled. Bound the short spin so longer gaps still use a blocking wait.
    for (unsigned attempt = 0; attempt < 256; ++attempt) {
        const T current = value.load(std::memory_order_acquire);
        if (current != observed)
            return current;
        _mm_pause();
    }
#endif
    value.wait(observed, std::memory_order_acquire);
    return value.load(std::memory_order_acquire);
}

class TaskScope {
  public:
    explicit TaskScope(unsigned index) : previous_(inside_task), previous_index_(task_index) {
        inside_task = true;
        task_index = index;
    }
    ~TaskScope() {
        inside_task = previous_;
        task_index = previous_index_;
    }
    TaskScope(const TaskScope&) = delete;
    TaskScope& operator=(const TaskScope&) = delete;

  private:
    bool previous_;
    unsigned previous_index_;
};

struct ParallelState {
    std::atomic<u64> generation{};
    std::atomic<unsigned> remaining{};
    std::atomic<bool> stopping{};
    std::array<std::exception_ptr, 3> errors{};
    unsigned worker_count{};
    s32 first{};
    s32 last{};
    const void* context{};
    RangeTask render{};
};

class ParallelRanges {
  public:
    ParallelRanges() {
        start();
    }

    ~ParallelRanges() noexcept {
        // Thread-local destructors can run while Windows is tearing a thread
        // down. Do not wait there; each worker keeps the shared state alive.
        stop(false);
    }

    ParallelRanges(const ParallelRanges&) = delete;
    ParallelRanges& operator=(const ParallelRanges&) = delete;

    void shutdown() {
        stop(true);
    }

    void run(s32 first, s32 last, const void* context, RangeTask render) {
        if (!state_)
            start();
        const unsigned worker_count = state_ ? state_->worker_count : 0U;
        const TaskScope scope{worker_count};
        if (worker_count == 0) {
            render(context, first, last, 0);
            return;
        }
        auto& state = *state_;
        state.first = first;
        state.last = last;
        state.context = context;
        state.render = render;
        state.errors.fill(nullptr);
        state.remaining.store(worker_count, std::memory_order_relaxed);
        // Publishing the generation makes this immutable job visible to every
        // worker. No following job is published until all workers have finished.
        state.generation.store(++generation_, std::memory_order_release);
        state.generation.notify_all();

        std::exception_ptr caller_error;
        try {
            render(context, boundary(state, worker_count), last, worker_count);
        } catch (...) {
            caller_error = std::current_exception();
        }
        unsigned remaining = state.remaining.load(std::memory_order_acquire);
        while (remaining != 0)
            remaining = wait_for_change(state.remaining, remaining);
        // Joining the job precedes exception propagation so no worker can retain
        // a reference to a caller's stack after this function returns.
        if (caller_error)
            std::rethrow_exception(caller_error);
        for (unsigned index = 0; index < worker_count; ++index) {
            if (state.errors[index])
                std::rethrow_exception(state.errors[index]);
        }
    }

  private:
    struct WorkerThreads {
        std::array<std::thread, 3> values;
    };

    void start() {
        std::shared_ptr<ParallelState> state;
        std::unique_ptr<WorkerThreads> workers;
        try {
            state = std::make_shared<ParallelState>();
            workers = std::make_unique<WorkerThreads>();
        } catch (const std::bad_alloc&) {
            return;
        }
        for (unsigned index = 0; index + 1U < parallel_capacity(); ++index) {
            try {
                workers->values[index] = std::thread([state, index] { work(state, index); });
                ++state->worker_count;
            } catch (const std::system_error&) {
                break;
            } catch (const std::bad_alloc&) {
                break;
            }
        }
        state_ = std::move(state);
        workers_ = std::move(workers);
    }

    void stop(bool wait) noexcept {
        auto state = std::move(state_);
        auto workers = std::move(workers_);
        if (!state)
            return;
        state->stopping.store(true, std::memory_order_relaxed);
        state->generation.store(++generation_, std::memory_order_release);
        state->generation.notify_all();
        bool retain_threads = false;
        for (unsigned index = 0; index < state->worker_count; ++index) {
            auto& worker = workers->values[index];
            if (!worker.joinable())
                continue;
            try {
                if (wait)
                    worker.join();
                else
                    worker.detach();
            } catch (...) {
                try {
                    worker.detach();
                } catch (...) {
                    retain_threads = true;
                }
            }
        }
        // A failed detach must not destroy a still-joinable std::thread from a
        // thread-local destructor. Retaining this tiny group is the safe fallback.
        if (retain_threads)
            static_cast<void>(workers.release());
    }

    static s32 boundary(const ParallelState& state, unsigned index) {
        const s64 length = static_cast<s64>(state.last) - state.first;
        return static_cast<s32>(static_cast<s64>(state.first) + length * index / (state.worker_count + 1U));
    }

    static void work(const std::shared_ptr<ParallelState>& state, unsigned index) {
        const TaskScope scope{index};
        u64 observed = 0;
        for (;;) {
            observed = wait_for_change(state->generation, observed);
            if (state->stopping.load(std::memory_order_relaxed))
                return;
            try {
                state->render(state->context, boundary(*state, index), boundary(*state, index + 1U), index);
            } catch (...) {
                state->errors[index] = std::current_exception();
            }
            if (state->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1U)
                state->remaining.notify_one();
        }
    }

    std::shared_ptr<ParallelState> state_;
    std::unique_ptr<WorkerThreads> workers_;
    u64 generation_{};
};

struct PoolSlot {
    std::optional<ParallelRanges> ranges;
};

thread_local PoolSlot pool;

} // namespace

void execute_parallel_ranges(s32 first, s32 last, const void* context, RangeTask render) {
    if (inside_task) {
        render(context, first, last, task_index);
        return;
    }
    // Each calling thread owns its workers. Concurrent machines do not serialize
    // on a global queue.
    if (!pool.ranges)
        pool.ranges.emplace();
    pool.ranges->run(first, last, context, render);
}

void shutdown_parallel_ranges() noexcept {
    if (inside_task || !pool.ranges)
        return;
    pool.ranges->shutdown();
    pool.ranges.reset();
}

} // namespace cupid::tasks
