#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace test::renderer_replay {
using ViRegisters = std::array<std::uint32_t, 14>;
inline ViRegisters base_video(unsigned format) {
  return {0x300u | format,    0x100000,          64, 0,   0,  0, 525, 3093, 0x0c150c15,
          (108u << 16) | 748, (34u << 16) | 514, 0,  102, 273};
}
inline std::vector<ViRegisters> video_variants(unsigned format) {
  std::vector<ViRegisters> result;
  const auto base = base_video(format);
  auto alter = [&](unsigned index, std::uint32_t value) {
    auto registers = base;
    registers[index] = value;
    result.push_back(registers);
  };
  for (unsigned control :
       {0u, 0x100u, 0x200u, 0x300u, 0x304u, 0x308u, 0x310u, 0x31cu, 0x10300u, 0xffff0000u})
    alter(0, control | format);
  for (unsigned width : {0u, 1u, 32u, 64u, 128u, 0xfff00040u})
    alter(2, width);
  for (unsigned scale :
       {0u, 256u, 512u, 1024u, 2048u, 4095u, 0x00800100u, 0x02000066u, 0xffff0066u})
    alter(12, scale);
  for (unsigned scale :
       {0u, 256u, 512u, 1024u, 2048u, 4095u, 0x00800111u, 0x02000111u, 0xffff0111u})
    alter(13, scale);
  for (unsigned range : {0u, (108u << 16) | 108, (748u << 16) | 108, (120u << 16) | 700,
                         (0u << 16) | 640, 0xffff02ecu})
    alter(9, range);
  for (unsigned range :
       {0u, (34u << 16) | 34, (514u << 16) | 34, (48u << 16) | 400, (0u << 16) | 480, 0xffff0202u})
    alter(10, range);
  for (unsigned origin : {0u, 0x100002u, 0x100008u, 0xff100000u})
    alter(1, origin);
  for (unsigned control : {0u, 1u})
    alter(0, control);
  for (unsigned lines : {0u, 524u, 525u, 625u})
    alter(6, lines);
  auto interlaced = base;
  interlaced[0] |= 0x40;
  interlaced[6] = 524;
  result.push_back(interlaced);
  return result;
}
inline std::uint16_t pixel16(unsigned x, unsigned y) {
  return static_cast<std::uint16_t>(((x * 3 & 31) << 11) | ((y * 5 & 31) << 6) |
                                    (((x ^ y) * 7 & 31) << 1) | ((x + y) & 1));
}
inline std::uint32_t pixel32(unsigned x, unsigned y) {
  return ((x * 17 & 255) << 24) | ((y * 29 & 255) << 16) | (((x ^ y) * 11 & 255) << 8) |
         (((x + y) & 7) << 5) | 31;
}

} // namespace test::renderer_replay
