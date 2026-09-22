#include "../tasks/parallel_ranges.hpp"
#include "cupid/bus.hpp"

#include <algorithm>

namespace cupid {

bool Rdp::parallel_rows(unsigned first, unsigned last, unsigned left, unsigned right) {
    if (!parallel_rasterization_ || tasks::parallel_capacity() == 1 || first >= last || left >= right ||
        right > color_image_width_ || color_image_size_ < 2 || !bus_.memory.direct_access_ready() ||
        static_cast<u64>(last - first) * (right - left) < 4096 || last - first < 8)
        return false;

    const u64 first_pixel = static_cast<u64>(first) * color_image_width_ + left;
    const u64 last_pixel = static_cast<u64>(last - 1U) * color_image_width_ + right;
    const unsigned bytes = 1U << (color_image_size_ - 1U);
    const u64 color_base = color_image_address_ & ~(bytes - 1U);
    const u64 color_first = color_base + first_pixel * bytes;
    const u64 color_last = color_base + last_pixel * bytes;
    if (color_last > bus_.rdram.size())
        return false;

    // Each task owns complete pixel and hidden-bit cells. Any wrapping or overlap
    // between color and depth would make a later row depend on an earlier row.
    if ((other_modes_ & 0x30U) != 0) {
        const u64 depth_base = depth_image_address_ & ~1U;
        const u64 depth_first = depth_base + first_pixel * 2U;
        const u64 depth_last = depth_base + last_pixel * 2U;
        if (depth_last > bus_.rdram.size() || (color_first < depth_last && depth_first < color_last))
            return false;
    }
    ++parallel_draws_;
    return true;
}

} // namespace cupid
