#pragma once

#include <array>
#include <cstdint>

namespace cupid::n64::disk_geometry {

inline constexpr unsigned PhysicalSize = 0x435b0c0;
inline constexpr unsigned LogicalSize = 0x3dec800;
inline constexpr unsigned Blocks = 0x10dc;
inline constexpr std::array<unsigned, 8> TrackStarts{0, 158, 316, 465, 614, 763, 912, 1061};
inline constexpr std::array<unsigned, 8> TrackCounts{158, 158, 149, 149, 149, 149, 149, 114};
inline constexpr std::array<unsigned, 9> SectorBytes{232, 216, 208, 192, 176, 160, 144, 128, 112};

constexpr unsigned block_size(unsigned zone) {
  return 85 * SectorBytes[(zone & 7) + (zone >> 3)];
}
constexpr unsigned zone_offset(unsigned zone) {
  unsigned offset = 0;
  for (unsigned previous = 0; previous < zone; ++previous)
    offset += TrackCounts[previous & 7] * block_size(previous) * 2;
  return offset;
}
constexpr unsigned track_zone(unsigned track) {
  unsigned zone = 0;
  while (zone < 7 && track >= TrackStarts[zone + 1])
    ++zone;
  return zone;
}
constexpr std::array<unsigned, 16> zone_order(unsigned type) {
  std::array<unsigned, 16> order{};
  unsigned next = 0;
  const unsigned first = type < 5 ? type + 3 : 8;
  const unsigned upper = type < 6 ? type + 9 : 15;
  for (unsigned zone = 0; zone < first; ++zone)
    order[next++] = zone;
  for (unsigned zone = upper + 1; zone > 8;)
    order[next++] = --zone;
  for (unsigned zone = first; zone < 8; ++zone)
    order[next++] = zone;
  for (unsigned zone = 16; zone > upper + 1;)
    order[next++] = --zone;
  return order;
}
constexpr unsigned sector_offset(unsigned track, unsigned head, unsigned sector, unsigned bytes) {
  const auto zone = track_zone(track);
  const auto physical = zone + head * 8;
  return zone_offset(physical) + (track - TrackStarts[zone]) * block_size(physical) * 2 +
         unsigned(sector >= 90) * block_size(physical) + (sector % 90) * bytes;
}

} // namespace cupid::n64::disk_geometry
