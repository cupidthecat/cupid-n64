#include "../../src/tasks/parallel_ranges.hpp"
#include "test.hpp"

#include <array>
#include <atomic>
#include <future>
#include <limits>
#include <stdexcept>

using namespace cupid;

TEST(parallel_ranges_join_repeated_jobs_without_missing_or_repeating_rows) {
    std::array<std::atomic<unsigned>, 257> visits{};
    std::array<unsigned, 257> values{};
    for (unsigned repeat = 0; repeat < 128; ++repeat) {
        tasks::parallel_ranges(0, 257, true, [&](s32 first, s32 last, unsigned) {
            for (s32 index = first; index < last; ++index) {
                const auto offset = static_cast<unsigned>(index);
                visits[offset].fetch_add(1, std::memory_order_relaxed);
                values[offset] = repeat;
            }
        });
        for (unsigned index = 0; index < values.size(); ++index) {
            CHECK_EQ(visits[index].load(std::memory_order_relaxed), repeat + 1U);
            CHECK_EQ(values[index], repeat);
        }
    }
}

TEST(parallel_ranges_cover_signed_boundaries_without_overflow) {
    struct Range {
        s32 first{}, last{};
        bool visited{};
    };
    std::array<Range, 4> ranges{};
    const s32 first = std::numeric_limits<s32>::min();
    const s32 last = std::numeric_limits<s32>::max();
    tasks::parallel_ranges(first, last, true,
                           [&](s32 begin, s32 end, unsigned index) { ranges[index] = {begin, end, true}; });
    s32 next = first;
    for (const auto& range : ranges) {
        if (!range.visited)
            continue;
        CHECK_EQ(range.first, next);
        CHECK(range.last > range.first);
        next = range.last;
    }
    CHECK_EQ(next, last);
}

TEST(parallel_ranges_finish_tasks_before_propagating_exceptions_and_remain_reusable) {
    struct Finished {
        std::atomic<unsigned>& count;
        ~Finished() {
            count.fetch_add(1, std::memory_order_relaxed);
        }
    };
    std::atomic<unsigned> warm_tasks{};
    tasks::parallel_ranges(0, 64, true,
                           [&](s32, s32, unsigned) { warm_tasks.fetch_add(1, std::memory_order_relaxed); });
    const unsigned task_count = warm_tasks.load(std::memory_order_relaxed);
    CHECK(task_count != 0);
    for (unsigned repeat = 0; repeat < 16; ++repeat) {
        std::atomic<unsigned> started{}, finished{};
        bool caught = false;
        try {
            tasks::parallel_ranges(0, 64, true, [&](s32, s32, unsigned index) {
                const Finished completion{finished};
                started.fetch_add(1, std::memory_order_relaxed);
                if (index == repeat % task_count)
                    throw std::runtime_error("render task failure");
            });
        } catch (const std::runtime_error&) {
            caught = true;
        }
        CHECK(caught);
        CHECK_EQ(started.load(std::memory_order_relaxed), task_count);
        CHECK_EQ(finished.load(std::memory_order_relaxed), task_count);
        std::atomic<unsigned> rows{};
        tasks::parallel_ranges(0, 64, true, [&](s32 first, s32 last, unsigned) {
            rows.fetch_add(static_cast<unsigned>(last - first), std::memory_order_relaxed);
        });
        CHECK_EQ(rows.load(std::memory_order_relaxed), 64U);
    }
}

TEST(parallel_ranges_keep_nested_jobs_and_concurrent_callers_independent) {
    std::array<std::future<void>, 3> callers;
    for (unsigned caller = 0; caller < callers.size(); ++caller) {
        callers[caller] = std::async(std::launch::async, [caller] {
            std::array<unsigned, 128> values{};
            for (unsigned repeat = 0; repeat < 8; ++repeat) {
                tasks::parallel_ranges(0, 128, true, [&](s32 first, s32 last, unsigned outer_index) {
                    tasks::parallel_ranges(first, last, true, [&](s32 begin, s32 end, unsigned inner_index) {
                        CHECK_EQ(inner_index, outer_index);
                        for (s32 row = begin; row < end; ++row)
                            values[static_cast<unsigned>(row)] = caller + repeat;
                    });
                });
                for (const auto value : values)
                    CHECK_EQ(value, caller + repeat);
            }
        });
    }
    for (auto& caller : callers)
        caller.get();
}
