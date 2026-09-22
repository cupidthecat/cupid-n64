#pragma once

#include "cupid/types.hpp"

#include <algorithm>
#include <memory>
#include <thread>

namespace cupid::tasks {

inline unsigned parallel_capacity() {
    static const unsigned capacity = std::min(4U, std::max(1U, std::thread::hardware_concurrency()));
    return capacity;
}

using RangeTask = void (*)(const void*, s32, s32, unsigned);

void execute_parallel_ranges(s32 first, s32 last, const void* context, RangeTask render);

template <typename Render> void parallel_ranges(s32 first, s32 last, bool parallel, const Render& render) {
    if (!parallel || parallel_capacity() == 1U || first >= last) {
        render(first, last, 0U);
        return;
    }
    execute_parallel_ranges(first, last, std::addressof(render),
                            [](const void* context, s32 begin, s32 end, unsigned index) {
                                (*static_cast<const Render*>(context))(begin, end, index);
                            });
}

} // namespace cupid::tasks
