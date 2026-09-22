#include "parallel_ranges.hpp"

#include <array>
#include <atomic>
#include <exception>
#include <new>
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

class ParallelRanges {
  public:
    ParallelRanges() {
        for (unsigned index = 0; index + 1U < parallel_capacity(); ++index) {
            try {
                workers_[index] = std::thread([this, index] { work(index); });
                ++worker_count_;
            } catch (const std::system_error&) {
                break;
            } catch (const std::bad_alloc&) {
                break;
            }
        }
    }

    ~ParallelRanges() {
        stopping_.store(true, std::memory_order_relaxed);
        generation_.fetch_add(1, std::memory_order_release);
        generation_.notify_all();
        for (unsigned index = 0; index < worker_count_; ++index)
            workers_[index].join();
    }

    ParallelRanges(const ParallelRanges&) = delete;
    ParallelRanges& operator=(const ParallelRanges&) = delete;

    void run(s32 first, s32 last, const void* context, RangeTask render) {
        const TaskScope scope{worker_count_};
        if (worker_count_ == 0) {
            render(context, first, last, 0);
            return;
        }
        first_ = first;
        last_ = last;
        context_ = context;
        render_ = render;
        errors_.fill(nullptr);
        remaining_.store(worker_count_, std::memory_order_relaxed);
        // Publishing the generation makes this immutable job visible to every
        // worker. No following job is published until all workers have finished.
        generation_.fetch_add(1, std::memory_order_release);
        generation_.notify_all();

        std::exception_ptr caller_error;
        try {
            render(context, boundary(worker_count_), last, worker_count_);
        } catch (...) {
            caller_error = std::current_exception();
        }
        unsigned remaining = remaining_.load(std::memory_order_acquire);
        while (remaining != 0)
            remaining = wait_for_change(remaining_, remaining);
        // Joining the job precedes exception propagation so no worker can retain
        // a reference to a caller's stack after this function returns.
        if (caller_error)
            std::rethrow_exception(caller_error);
        for (unsigned index = 0; index < worker_count_; ++index) {
            if (errors_[index])
                std::rethrow_exception(errors_[index]);
        }
    }

  private:
    s32 boundary(unsigned index) const {
        const s64 length = static_cast<s64>(last_) - first_;
        return static_cast<s32>(static_cast<s64>(first_) + length * index / (worker_count_ + 1U));
    }

    void work(unsigned index) {
        const TaskScope scope{index};
        u64 observed = 0;
        for (;;) {
            observed = wait_for_change(generation_, observed);
            if (stopping_.load(std::memory_order_relaxed))
                return;
            try {
                render_(context_, boundary(index), boundary(index + 1U), index);
            } catch (...) {
                errors_[index] = std::current_exception();
            }
            if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1U)
                remaining_.notify_one();
        }
    }

    std::atomic<u64> generation_{};
    std::atomic<unsigned> remaining_{};
    std::atomic<bool> stopping_{};
    std::array<std::exception_ptr, 3> errors_{};
    std::array<std::thread, 3> workers_;
    unsigned worker_count_{};
    s32 first_{};
    s32 last_{};
    const void* context_{};
    RangeTask render_{};
};

} // namespace

void execute_parallel_ranges(s32 first, s32 last, const void* context, RangeTask render) {
    if (inside_task) {
        render(context, first, last, task_index);
        return;
    }
    // Each calling thread owns its workers. Concurrent machines do not serialize
    // on a global queue, and a worker exits when its owning thread exits.
    thread_local ParallelRanges ranges;
    ranges.run(first, last, context, render);
}

} // namespace cupid::tasks
