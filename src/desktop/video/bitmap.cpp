#include "desktop/video/bitmap.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace cupid::desktop {

std::vector<std::uint8_t> bitmap_image(unsigned width, unsigned height,
                                       std::span<const std::uint8_t> bgra) {
  const auto pixels = std::uint64_t(width) * height;
  const auto stride = (std::uint64_t(width) * 3 + 3) & ~std::uint64_t(3);
  const auto bytes = stride * height;
  if (!width || !height || width > 32768 || height > 32768 || pixels * 4 != bgra.size() ||
      bytes > std::numeric_limits<std::uint32_t>::max() - 54)
    throw std::runtime_error("The displayed image has invalid screenshot dimensions.");
  std::vector<std::uint8_t> result(static_cast<std::size_t>(54 + bytes));
  const auto word = [&](unsigned offset, std::uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte)
      result[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
  };
  result[0] = 'B';
  result[1] = 'M';
  word(2, static_cast<std::uint32_t>(result.size()));
  word(10, 54);
  word(14, 40);
  word(18, width);
  word(22, height);
  result[26] = 1;
  result[28] = 24;
  word(34, static_cast<std::uint32_t>(bytes));
  for (unsigned y = 0; y < height; ++y) {
    for (unsigned x = 0; x < width; ++x) {
      const auto source = (std::size_t(height - y - 1) * width + x) * 4;
      const auto target = static_cast<std::size_t>(54 + stride * y + x * 3);
      std::copy_n(bgra.data() + source, 3, result.data() + target);
    }
  }
  return result;
}

} // namespace cupid::desktop
