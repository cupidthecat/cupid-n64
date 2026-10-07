#include "core/disk/image/image.hpp"
#include "../fixture.hpp"
#include "core/disk/image/geometry.hpp"
#include <algorithm>

namespace test {
using namespace cupid::n64;

void disk_image_tests() {
  using namespace disk_geometry;
  equal(zone_offset(8), 0x23196e0);
  equal(zone_offset(16), PhysicalSize);
  equal(sector_offset(158, 0, 90, 216), 0x5f15e0 + 0x47b8);
  equal(sector_offset(1061, 1, 0, 112), 0x4149200);
  for (unsigned type = 0; type < 7; ++type) {
    const auto order = zone_order(type);
    auto sorted = order;
    std::sort(sorted.begin(), sorted.end());
    for (unsigned zone = 0; zone < 16; ++zone)
      equal(sorted[zone], zone);
    unsigned blocks = 0, total = 0;
    for (const auto zone : order) {
      const auto count = (TrackCounts[zone & 7] - 12) * 2;
      blocks += count;
      total += count * block_size(zone);
    }
    equal(blocks, Blocks);
    equal(total, LogicalSize);
    std::vector<std::uint8_t> compact(512 + block_size(0));
    compact[5] = static_cast<std::uint8_t>(type);
    compact[0x1c] = 0x80;
    compact[0xe2] = compact[0xe3] = compact[0xe4] = compact[0xe5] = 255;
    std::copy_n("DISK", 4, compact.begin() + 0x100);
    for (unsigned byte = 512; byte < compact.size(); ++byte)
      compact[byte] = static_cast<std::uint8_t>(byte ^ 0x5a);
    DiskImage image;
    equal(image.load(compact), true);
    equal(image.data.size(), PhysicalSize);
    equal(image.errors[0], 1);
    equal(image.errors[2], 0);
    equal(image.errors[12], 0);
    equal(image.errors[14], 0);
    const auto offset = 12 * block_size(0) * 2;
    for (unsigned byte = 0; byte < block_size(0); ++byte)
      equal(image.data[offset + byte], static_cast<std::uint8_t>(byte ^ 0x5a));
    for (unsigned sector = 0; sector < 85; ++sector) {
      equal(image.data[2 * block_size(0) + sector * 192 + 4], 0x10);
      equal(image.data[2 * block_size(0) + sector * 192 + 5], 0x10 + type);
      equal(image.data[14 * block_size(0) + sector * 232], 'D');
    }
    compact[0xe4] = 0;
    equal(image.load(compact), false);
    equal(image.data.empty(), true);
    compact[0xe4] = 255;
    compact[0x20] = 1;
    equal(image.load(compact), false);
  }
}

} // namespace test
