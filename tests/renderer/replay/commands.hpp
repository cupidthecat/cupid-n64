#pragma once
#include "video.hpp"
#include <algorithm>
#include <string>

namespace test::renderer_replay {
using RdpPacket = std::array<std::uint32_t, 2>;
constexpr std::uint32_t color_address = 0x100000, depth_address = 0x110000,
                        texture_address = 0x120000, palette_address = 0x130000;
struct RenderCase {
  std::string name;
  unsigned framebuffer = 2, opcode = 8, texture = 0, filter = 0, cycle = 0, load = 0;
  std::uint32_t modes = 0;
  bool rectangle = false, flip = false;
  unsigned combiner = 0, layer = 0;
};
struct TextureKind {
  unsigned format, size;
};
inline constexpr TextureKind texture_kinds[] = {{0, 2}, {0, 3}, {1, 2}, {2, 0}, {2, 1},
                                                {3, 0}, {3, 1}, {3, 2}, {4, 0}, {4, 1}};
inline RdpPacket packet64(std::uint64_t word) {
  return {std::uint32_t(word >> 32), std::uint32_t(word)};
}
inline RdpPacket direct_combine(unsigned source) {
  return {0x3c000000 | (8u << 20) | (31u << 15) | (7u << 12) | (7u << 9) | (8u << 5) | 31,
          (8u << 28) | (8u << 24) | (7u << 21) | (7u << 18) | (source << 15) | (7u << 12) |
              (source << 9) | (source << 6) | (7u << 3) | source};
}
inline RdpPacket arithmetic_combine(unsigned recipe, bool two_cycles) {
  using Inputs = std::array<unsigned, 8>;
  const Inputs choices[] = {{1, 8, 4, 5, 1, 7, 4, 7},  {3, 5, 1, 5, 3, 5, 1, 5},
                            {4, 3, 5, 3, 4, 3, 5, 3},  {7, 8, 3, 5, 3, 7, 7, 3},
                            {1, 4, 10, 5, 1, 7, 3, 5}, {3, 8, 14, 5, 3, 7, 6, 5},
                            {6, 8, 1, 7, 6, 7, 1, 7},  {5, 8, 4, 3, 5, 7, 4, 3}};
  auto first = choices[(recipe - 1) % std::size(choices)], second = first;
  if (two_cycles)
    second = {0, 5, 4, 3, 0, 5, 4, 3};
  return {0x3c000000 | (first[0] << 20) | (first[2] << 15) | (first[4] << 12) | (first[6] << 9) |
              (second[0] << 5) | second[2],
          (first[1] << 28) | (second[1] << 24) | (second[4] << 21) | (second[6] << 18) |
              (first[3] << 15) | (first[5] << 12) | (first[7] << 9) | (second[3] << 6) |
              (second[5] << 3) | second[7]};
}
inline RdpPacket tile(unsigned index, TextureKind kind, unsigned line, unsigned address,
                      unsigned palette = 0) {
  return {0x35000000 | (kind.format << 21) | (kind.size << 19) | (line << 9) | address,
          (index << 24) | (palette << 20) | (2u << 18) | (3u << 14) | (2u << 8) | (3u << 4)};
}
inline std::vector<RdpPacket> texture_setup(const RenderCase &input) {
  std::vector<RdpPacket> words;
  const auto kind = texture_kinds[input.texture];
  if (kind.format == 2) {
    words.push_back({0x3d100000, palette_address});
    words.push_back(tile(7, {0, 2}, 0, 256));
    words.push_back({0x30000000, 0x07000000 | (1020u << 12)});
  }
  const unsigned bits = 4u << kind.size;
  const unsigned line = (8 * std::min(bits, 16u) + 63) / 64;
  const bool packed = kind.size == 0;
  const auto transfer = packed ? TextureKind{0, 2} : kind;
  words.push_back(
      {0x3d000000 | (packed ? 3u : 7u) | (transfer.format << 21) | (transfer.size << 19),
       texture_address});
  words.push_back(tile(7, transfer, line, 0));
  if (input.load == 0)
    words.push_back({0x34000000, 0x07000000 | ((packed ? 12u : 28u) << 12) | 28});
  else
    words.push_back({0x33000000, 0x07000000 | ((packed ? 31u : 63u) << 12) |
                                     std::uint32_t((2048 + line - 1) / line)});
  words.push_back(tile(0, kind, line, 0));
  words.push_back({0x32000000, (28u << 12) | 28});
  return words;
}
inline std::vector<RdpPacket> triangle(unsigned opcode, std::uint32_t z = 0x40000000) {
  std::vector<RdpPacket> words = {{(opcode << 24) | 0x00800000 | 48, (24u << 16) | 4},
                                  {12u << 16, std::uint32_t(-87381)},
                                  {1u << 16, 17873},
                                  {1u << 16, 144179}};
  if (opcode & 4) {
    std::array<std::uint32_t, 16> shade{};
    shade[0] = (180u << 16) | 40;
    shade[1] = (220u << 16) | 160;
    for (unsigned n = 0; n < shade.size(); n += 2)
      words.push_back({shade[n], shade[n + 1]});
  }
  if (opcode & 2) {
    std::array<std::uint32_t, 16> texture{};
    texture[1] = 1u << 16;
    texture[2] = 32u << 16;
    texture[8] = (9u << 16) | 32;
    texture[10] = 32;
    for (unsigned n = 0; n < texture.size(); n += 2)
      words.push_back({texture[n], texture[n + 1]});
  }
  if (opcode & 1) {
    words.push_back({z, 0});
    words.push_back({0, 0});
  }
  return words;
}
inline std::vector<RdpPacket> render_commands(const RenderCase &input, unsigned &draw_start) {
  const bool fill = input.opcode == 0x36;
  const bool textured = !fill && (input.rectangle || bool(input.opcode & 2));
  const auto kind = texture_kinds[input.texture];
  unsigned high = (input.cycle << 20) | 0xf0;
  if (textured && kind.format == 2)
    high |= 0x8000;
  if (input.filter == 1)
    high |= 0x2800;
  if (input.filter == 2)
    high |= 0x3800;
  std::vector<RdpPacket> words = {
      {0x3f00000f | (input.framebuffer << 19), color_address},
      {0x3e000000, depth_address},
      {0x2d000000, 0x00040040},
      {0x2f000000 | high, input.layer ? input.modes & ~0xc00u : input.modes},
      {0x3a000080, 0x40e080a0},
      {0x3b000000, 0xe04060c0},
      {0x38000000, 0x204080c0},
      {0x39000000, 0x80808080},
      input.combiner ? arithmetic_combine(input.combiner, input.cycle == 1)
                     : direct_combine(textured           ? 1
                                      : input.opcode & 4 ? 4
                                                         : 3),
      {0x2e000000, 0x40000008},
      {0x37000000, input.framebuffer == 2 ? 0xf80107c1u : 0xe04060ffu}};
  std::uint64_t convert = std::uint64_t(0x2c) << 56;
  const int coefficients[] = {175, -43, -89, 222, 114, 42};
  for (unsigned n = 0; n < 6; ++n)
    convert |= std::uint64_t(coefficients[n] & 511) << (45 - n * 9);
  words.push_back(packet64(convert));
  if (textured) {
    auto setup = texture_setup(input);
    words.insert(words.end(), setup.begin(), setup.end());
  }
  draw_start = unsigned(words.size());
  if (fill) {
    words.push_back({0x3602802c, 0x00004004});
  } else if (input.rectangle) {
    words.push_back({(input.flip ? 0x25000000u : 0x24000000u) | (40u << 12) | 44, (4u << 12) | 4});
    words.push_back({(16u << 16) | 16, ((input.cycle == 2 ? 4096u : 1024u) << 16) | 1024});
  } else {
    auto draw = triangle(input.opcode);
    words.insert(words.end(), draw.begin(), draw.end());
  }
  if (!fill && (input.modes & 0x30)) {
    const auto second_z = input.layer == 2   ? 0x40000000u
                          : input.layer == 3 ? 0x70000000u
                                             : 0x10000000u;
    words.push_back({0x2f000000 | high, input.modes});
    words.push_back({0x3a000000, 0xe040a080});
    words.push_back({0x2e000000, (second_z >> 16 << 16) | 16});
    words.push_back(direct_combine(3));
    auto second = triangle(9, second_z);
    words.insert(words.end(), second.begin(), second.end());
  }
  return words;
}
inline std::vector<RenderCase> render_cases() {
  std::vector<RenderCase> cases;
  for (unsigned framebuffer : {2u, 3u}) {
    for (unsigned opcode = 8; opcode < 16; ++opcode)
      cases.push_back({"triangle", framebuffer, opcode, 0, 0, 0, 0, opcode & 1 ? 0x30u : 0u});
    for (unsigned texture = 0; texture < std::size(texture_kinds); ++texture)
      for (unsigned filter : {0u, 1u, 2u})
        for (unsigned load : {0u, 1u})
          for (bool flip : {false, true})
            cases.push_back(
                {"texture-rectangle", framebuffer, 8, texture, filter, 0, load, 0, true, flip});
    for (unsigned cycle : {1u, 2u})
      if (framebuffer != 3 || cycle != 2)
        cases.push_back({"texture-cycle", framebuffer, 8, 0, 1, cycle, 0, 0, true, false});
    for (unsigned modes :
         {8u, 0x40u | 0x4000u | (1u << 22), 0x1008u, 0x2008u, 0x3008u, 0x0108u, 0x0208u, 0x0308u,
          0x0088u, 0x00c8u, 0x0030u, 0x0430u, 0x0830u, 0x0c30u, 1u, 2u, 3u})
      cases.push_back({"pipeline", framebuffer, 13, 0, 0, 0, 0, modes});
    cases.push_back({"fill-rectangle", framebuffer, 0x36, 0, 0, 3});
    for (unsigned cycle : {0u, 1u})
      for (unsigned recipe = 1; recipe <= 8; ++recipe) {
        RenderCase input{"combiner", framebuffer, 14, 0, 1, cycle};
        input.combiner = recipe;
        cases.push_back(input);
      }
    for (unsigned cycle : {0u, 1u})
      for (unsigned a = 0; a < 4; ++a)
        for (unsigned b = 0; b < 4; ++b)
          for (unsigned c = 0; c < 4; ++c)
            for (unsigned d = 0; d < 4; ++d) {
              const auto mux = (a << 30) | (a << 28) | (b << 26) | (b << 24) | (c << 22) |
                               (c << 20) | (d << 18) | (d << 16);
              cases.push_back({"blender", framebuffer, 12, 0, 0, cycle, 0, mux | 0x4048});
            }
    for (unsigned mode = 0; mode < 4; ++mode)
      for (unsigned layer = 1; layer <= 3; ++layer)
        for (bool primitive : {false, true}) {
          RenderCase input{"depth-layer",
                           framebuffer,
                           9,
                           0,
                           0,
                           0,
                           0,
                           0x30u | (mode << 10) | (primitive ? 4u : 0u)};
          input.layer = layer;
          cases.push_back(input);
        }
  }
  return cases;
}
template <class Memory> void initialize_render(Memory &memory, const RenderCase &input) {
  for (unsigned n = 0; n < 0x10000; n += 4) {
    memory.put(color_address + n, 0);
    memory.put(depth_address + n, 0xfffcfffc);
  }
  for (unsigned n = 0; n < 0x8000; ++n) {
    memory.hidden(color_address / 2 + n, 3);
    memory.hidden(depth_address / 2 + n, 3);
  }
  const auto kind = texture_kinds[input.texture];
  std::vector<std::uint8_t> bytes;
  const auto append_byte = [&](auto value) { bytes.push_back(static_cast<std::uint8_t>(value)); };
  for (unsigned y = 0; y < 8; ++y)
    for (unsigned x = 0; x < 8; ++x) {
      const unsigned index = x + y * 8;
      if (kind.format == 0 && kind.size == 2) {
        auto color = pixel16(x * 2, y * 3) | 1;
        append_byte(color >> 8);
        append_byte(color);
      } else if (kind.format == 0) {
        auto color = pixel32(x * 2, y * 3) | 255;
        for (unsigned shift : {24u, 16u, 8u, 0u})
          append_byte(color >> shift);
      } else if (kind.format == 1) {
        append_byte((x & 1) ? 160 + y * 5 : 64 + y * 11);
        append_byte(32 + x * 23 + y * 9);
      } else if (kind.size == 2) {
        append_byte(32 + x * 23 + y * 9);
        append_byte((x + y) & 1 ? 255 : 96);
      } else if (kind.size == 1)
        append_byte(kind.format == 2   ? index * 3
                    : kind.format == 3 ? ((index & 15) << 4) | ((index & 1) ? 15 : 7)
                                       : index * 3);
      else {
        auto nibble = kind.format == 2   ? index & 15
                      : kind.format == 3 ? ((index & 7) << 1) | (index & 1)
                                         : index & 15;
        if (!(x & 1))
          append_byte(nibble << 4);
        else
          bytes.back() |= nibble;
      }
      if (kind.size == 0 && x == 7)
        for (unsigned padding = 0; padding < 4; ++padding)
          bytes.push_back(0);
    }
  while (bytes.size() % 4)
    bytes.push_back(0);
  for (unsigned n = 0; n < bytes.size(); n += 4)
    memory.put(texture_address + n, (std::uint32_t(bytes[n]) << 24) |
                                        (std::uint32_t(bytes[n + 1]) << 16) |
                                        (std::uint32_t(bytes[n + 2]) << 8) | bytes[n + 3]);
  for (unsigned n = 0; n < 256; n += 2)
    memory.put(palette_address + n * 2,
               (std::uint32_t(pixel16(n, n / 5) | 1) << 16) | (pixel16(n + 1, (n + 1) / 5) | 1));
  auto video = base_video(input.framebuffer);
  video[2] = 16;
  video[12] = 25;
  video[13] = 68;
  memory.program(video);
}

} // namespace test::renderer_replay
