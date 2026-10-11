#include "desktop/video/viewport.hpp"
#include <algorithm>
#include <cstdint>

namespace cupid::desktop {

Viewport image_viewport(unsigned image_width, unsigned image_height, unsigned area_width,
                        unsigned area_height, DisplayScale scale, DisplayAspect aspect) {
  if (!image_width || !image_height || !area_width || !area_height)
    return {};
  if (scale == DisplayScale::Stretch)
    return {0, 0, area_width, area_height};
  const std::uint64_t ratio_width = aspect == DisplayAspect::Standard ? 4 : image_width;
  const std::uint64_t ratio_height = aspect == DisplayAspect::Standard ? 3 : image_height;
  std::uint64_t width = area_width, height = area_height;
  if (width * ratio_height > height * ratio_width)
    width = height * ratio_width / ratio_height;
  else
    height = width * ratio_height / ratio_width;
  if (scale == DisplayScale::Integer) {
    const std::uint64_t base_height = image_height;
    const auto base_width =
        aspect == DisplayAspect::Standard ? (base_height * 4 + 1) / 3 : std::uint64_t(image_width);
    const auto factor = std::min(area_width / base_width, area_height / base_height);
    if (factor) {
      width = base_width * factor;
      height = base_height * factor;
    }
  }
  return {static_cast<unsigned>((area_width - width) / 2),
          static_cast<unsigned>((area_height - height) / 2), static_cast<unsigned>(width),
          static_cast<unsigned>(height)};
}

} // namespace cupid::desktop
