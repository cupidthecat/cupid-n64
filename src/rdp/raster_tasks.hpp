#pragma once

#include "../tasks/parallel_ranges.hpp"
#include "cupid/rdram.hpp"

#include <array>
#include <atomic>

namespace cupid::rdp {

template <typename Render>
void raster_rows(Rdram& memory, unsigned first, unsigned last, bool parallel, const Render& render) {
    if (!parallel) {
        render(first, last);
        return;
    }
    constexpr unsigned maximum_ranges = 16;
    std::array<Rdram::BankAccessSummary, maximum_ranges> summaries{};
    const unsigned count = std::min(maximum_ranges, last - first);
    std::atomic<unsigned> next{};
    // Triangle rows can have very different widths. Small contiguous ranges let
    // a finished worker help with the remaining rows without sharing pixels.
    tasks::parallel_ranges(0, static_cast<s32>(tasks::parallel_capacity()), true, [&](s32, s32, unsigned) {
        for (;;) {
            const unsigned index = next.fetch_add(1, std::memory_order_relaxed);
            if (index >= count)
                break;
            const unsigned begin = first + (last - first) * index / count;
            const unsigned end = first + (last - first) * (index + 1U) / count;
            const Rdram::BankAccessScope scope(memory, summaries[index]);
            render(begin, end);
        }
    });
    // Hardware bank effects follow raster order even when host ranges finish
    // out of order.
    for (unsigned index = 0; index < count; ++index)
        memory.merge_bank_accesses(summaries[index]);
}

} // namespace cupid::rdp
