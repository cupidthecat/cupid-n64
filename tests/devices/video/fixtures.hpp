#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace test::video {

using Registers = std::array<std::uint32_t, 14>;

struct ScanoutCase {
  bool pal = false;
  bool expansion = true;
  Registers registers;
};

inline std::vector<ScanoutCase> scanout_cases() {
  std::vector<ScanoutCase> cases;
  for (bool pal : {false, true}) {
    for (unsigned depth : {2u, 3u}) {
      const unsigned h = pal ? 128 : 108;
      const unsigned v = pal ? 44 : 34;
      Registers base{depth | 64,           0x1000,     37,   48,         0,
                     0x03e52239,           524,        3092, 0x0c150c15, (h << 16) | (h + 48),
                     (v << 16) | (v + 16), 0x000e0204, 1024, 2048};
      const auto add = [&](unsigned index, std::uint32_t value) {
        auto registers = base;
        registers[index] = value;
        cases.push_back({pal, true, registers});
      };
      cases.push_back({pal, true, base});
      for (unsigned scale : {0u, 1u, 256u, 511u, 512u, 1023u, 1024u, 1025u, 1536u, 2048u, 4095u}) {
        add(12, scale);
        add(13, scale);
      }
      for (unsigned offset : {1u, 255u, 511u, 512u, 1023u, 1024u, 2047u, 4095u}) {
        add(12, (offset << 16) | 1024);
        add(13, (offset << 16) | 2048);
      }
      for (auto range : {std::array{0u, 0u}, std::array{0u, 1023u}, std::array{h - 8, h + 48},
                         std::array{h, h + 15}, std::array{h, h + 16}, std::array{h + 20, h + 48},
                         std::array{h, h + 640}, std::array{h + 630, h + 640},
                         std::array{h + 640, 1023u}, std::array{900u, 100u}})
        add(9, (range[0] << 16) | range[1]);
      for (auto range : {std::array{0u, 0u}, std::array{0u, v + 16}, std::array{v, v},
                         std::array{v, v + 1}, std::array{v + 1, v + 16}, std::array{v + 12, v + 4},
                         std::array{v, v + (pal ? 576u : 480u)}, std::array{700u, 900u},
                         std::array{1023u, 1023u}})
        add(10, (range[0] << 16) | range[1]);
      for (unsigned width : {0u, 1u, 17u, 64u, 4095u})
        add(2, width);
      for (unsigned origin : {0u, 0x1001u, 0x1002u, 0x1003u, 0x7ffff0u, 0x800000u, 0xffffffu})
        add(1, origin);
      for (unsigned control :
           {0u, 1u, depth, depth | 4, depth | 8, depth | 16, depth | 0x300, depth | 0xfffffffcu})
        add(0, control);
      cases.push_back({pal, false, base});
      auto edge = base;
      edge[1] = 0x3ffff0;
      cases.push_back({pal, false, edge});
      edge[1] = 0x400000;
      cases.push_back({pal, false, edge});
    }
  }
  return cases;
}

inline std::uint32_t framebuffer_word(unsigned index, unsigned phase) {
  const auto a = (index * 13 + phase * 67 + 19) & 255;
  const auto b = (index * 47 + phase * 29 + 37) & 255;
  const auto c = (index * 71 + phase * 43 + 53) & 255;
  return (a << 24) | (b << 16) | (c << 8) | ((index + phase) & 255);
}

template <typename Write>
void fill_framebuffer(const ScanoutCase &input, unsigned phase, Write write) {
  const unsigned size = input.expansion ? 0x800000 : 0x400000;
  const auto origin = input.registers[1] & ~3u;
  for (unsigned n = 0; n < 2048; ++n) {
    const auto address = origin + n * 4;
    if (address < size)
      write(address, framebuffer_word(n, phase));
  }
}

} // namespace test::video
