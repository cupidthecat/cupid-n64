#include "geometry.hpp"
#include "image.hpp"
#include <algorithm>

namespace cupid::n64 {

bool DiskImage::map(std::span<const std::uint8_t> input, bool logical, bool compact) {
  using namespace disk_geometry;
  if (!logical && !compact) {
    data.assign(input.begin(), input.end());
    return true;
  }
  unsigned header_offset = 0;
  if (logical) {
    bool found = false;
    for (const auto block : {9u, 8u, 1u, 0u}) {
      const auto system = block + (errors[12] ? 0 : 2);
      if (!errors[system]) {
        found = true;
        header_offset = (errors[12] ? system : system ^ 1) * block_size(0);
      }
    }
    if (!found)
      return false;
  }
  const auto header = input.subspan(header_offset, 232);
  const auto type = unsigned(header[5] & 15);
  if (type >= 7)
    return false;
  const auto half = [&](unsigned address) {
    return (unsigned(input[address]) << 8) | input[address + 1];
  };
  const auto rom_end = compact ? half(0xe0) + 24 : Blocks;
  unsigned ram_start = compact ? half(0xe2) : Blocks, ram_end = compact ? half(0xe4) : Blocks;
  if (compact) {
    if ((ram_start == 65535) != (ram_end == 65535))
      return false;
    if (ram_start != 65535) {
      ram_start += 24;
      ram_end += 24;
    }
  }
  data.assign(PhysicalSize, 0);
  if (compact) {
    for (const auto block : {2u, 3u, 10u, 11u}) {
      const auto offset = block * block_size(0);
      std::copy_n(header.begin(), 192, data.begin() + offset);
      data[offset + 4] = 0x10;
      data[offset + 5] += 0x10;
      for (unsigned sector = 1; sector < 85; ++sector)
        std::copy_n(data.begin() + offset, 192, data.begin() + offset + sector * 192);
    }
    for (const auto block : {14u, 15u})
      for (unsigned sector = 0; sector < 85; ++sector)
        std::copy_n(input.begin() + 0x100, 232,
                    data.begin() + block * block_size(0) + sector * 232);
  }
  std::size_t cursor = compact ? 512 : 0;
  unsigned lba = 0;
  for (const auto physical : zone_order(type)) {
    const auto head = physical >> 3, zone = physical & 7;
    const auto count = (TrackCounts[zone] - 12) * 2;
    for (unsigned relative = 0; relative < count; ++relative, ++lba) {
      if (compact && (lba < 24 || (lba > rom_end && lba < ram_start) || lba > ram_end))
        continue;
      auto track = head ? TrackStarts[zone] + TrackCounts[zone] - 13 - relative / 2
                        : TrackStarts[zone] + relative / 2;
      unsigned defect = physical ? header[7 + physical] : 0;
      const unsigned end = header[8 + physical];
      if (end < defect || end > 200)
        return false;
      while (defect < end && header[32 + defect] + TrackStarts[zone] <= track) {
        ++track;
        ++defect;
      }
      const auto bytes = block_size(physical);
      const auto block = ((lba + 1) >> 1) & 1;
      const auto offset =
          zone_offset(physical) + (track - TrackStarts[zone]) * bytes * 2 + block * bytes;
      if (offset > data.size() || bytes > data.size() - offset || cursor > input.size() ||
          bytes > input.size() - cursor)
        return false;
      std::copy_n(input.begin() + cursor, bytes, data.begin() + offset);
      cursor += bytes;
    }
  }
  return lba == Blocks && cursor == input.size();
}

} // namespace cupid::n64
