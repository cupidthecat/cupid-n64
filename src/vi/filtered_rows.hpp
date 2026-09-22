#pragma once

#include "cupid/vi/filter.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <vector>

namespace cupid::vi {

// A scan owns its row samples. Framebuffer writes and register changes are visible
// to the next scan without any cross-frame invalidation state.
class FilteredRows {
  public:
    FilteredRows(const ViFilter& filter, s32 first_x, unsigned width)
        : filter_(filter), first_x_(first_x), width_(width) {}

    std::span<const ViColor> get(s32 y, bool repeat_lower) {
        for (auto& row : rows_) {
            if (row.used != 0 && row.y == y && row.repeat_lower == repeat_lower) {
                row.used = ++sequence_;
                return row.colors;
            }
        }
        auto& row = *std::min_element(rows_.begin(), rows_.end(),
                                      [](const auto& a, const auto& b) { return a.used < b.used; });
        row.colors.resize(width_);
        filter_.sample_row(first_x_, y, repeat_lower, row.colors);
        row.y = y;
        row.repeat_lower = repeat_lower;
        row.used = ++sequence_;
        return row.colors;
    }

  private:
    struct Row {
        std::vector<ViColor> colors;
        s32 y{};
        bool repeat_lower{};
        unsigned used{};
    };
    const ViFilter& filter_;
    s32 first_x_;
    unsigned width_;
    unsigned sequence_{};
    std::array<Row, 4> rows_;
};

} // namespace cupid::vi
