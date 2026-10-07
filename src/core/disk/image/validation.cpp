#include "geometry.hpp"
#include "image.hpp"
#include <algorithm>

namespace cupid::n64 {
namespace {

bool repeated(std::span<const std::uint8_t> block, unsigned bytes) {
  for (unsigned sector = 1; sector < 85; ++sector)
    if (!std::equal(block.begin(), block.begin() + bytes, block.begin() + sector * bytes))
      return false;
  return true;
}

bool system_header(std::span<const std::uint8_t> block, bool development) {
  if (development) {
    if (std::any_of(block.begin(), block.begin() + 4, [](auto byte) { return byte != 0; }))
      return false;
  } else {
    constexpr std::uint8_t japan[]{0xe8, 0x48, 0xd3, 0x16}, north_america[]{0x22, 0x63, 0xee, 0x56};
    for (unsigned byte = 0; byte < 4; ++byte)
      if (block[byte] != japan[byte] && block[byte] != north_america[byte])
        return false;
  }
  return block[4] == 0x10 && block[5] >= 0x10 && block[5] < 0x17 && block[0x18] == 255 &&
         block[0x19] == 255 && block[0x1a] == 255 && block[0x1b] == 255 && block[0x1c] == 0x80;
}

} // namespace

bool DiskImage::validate(std::span<const std::uint8_t> input, bool logical, bool compact) {
  errors.assign(4700, 0);
  constexpr unsigned system_blocks[]{0, 1, 8, 9};
  if (compact) {
    for (unsigned offset = 0; offset < 512; ++offset) {
      const bool defined = (offset >= 5 && offset <= 7) || (offset >= 0x1c && offset <= 0x1f) ||
                           (offset >= 0xe0 && offset <= 0xe5) ||
                           (offset >= 0x100 && offset < 0x120) || offset == 0x1e8;
      if (!defined && input[offset])
        return false;
    }
    if (input[5] >= 7 || input[6] >= 2 || input[0x1c] != 0x80 || input[0x1d] >= 0x80 ||
        input[0xe0] > 0x10 || (input[0xe2] > 0x10 && input[0xe2] != 255) ||
        (input[0xe4] > 0x10 && input[0xe4] != 255))
      return false;
    for (const auto block : system_blocks)
      errors[block] = 1;
    return true;
  }
  for (const auto block : system_blocks) {
    const auto bytes = input.subspan(block * disk_geometry::block_size(0));
    errors[block] = !system_header(bytes, false) || !repeated(bytes, 232);
    if (!errors[block])
      errors[12] = 1;
  }
  if (!errors[12]) {
    for (const auto block : system_blocks) {
      const auto position = logical ? ((block + 2) ^ 1) : block + 2;
      const auto bytes = input.subspan(position * disk_geometry::block_size(0));
      errors[block + 2] = !system_header(bytes, true) || !repeated(bytes, 192);
    }
  }
  for (unsigned block = 14; block <= 15; ++block)
    errors[block] = !repeated(input.subspan(block * disk_geometry::block_size(0)), 232);
  return true;
}

} // namespace cupid::n64
