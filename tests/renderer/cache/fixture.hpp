#pragma once

#include "../../support/test.hpp"
#include "core/system/console.hpp"
#include "renderer/vulkan/renderer.hpp"
#include <array>
#include <utility>
#include <vector>

namespace test::renderer_cache {
using namespace cupid::n64;

using Packet = std::array<std::uint32_t, 2>;
constexpr std::uint32_t target = 0x110000;
constexpr std::uint64_t cached = 0xffffffff80110000;
constexpr std::uint64_t direct = 0xffffffffa0110000;
constexpr std::uint64_t red_frame = 1192205476866970405ull;
constexpr std::uint64_t blue_frame = 4202700852216336549ull;

inline std::uint64_t extended(std::uint32_t word) {
  return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(word)));
}

inline std::vector<Packet> surface(std::uint32_t fill) {
  return {{0x3f10003f, 0x180000},
          {0x2d000000, 0x00100100},
          {0x2f300000, 0},
          {0x37000000, fill},
          {0x360fc0fc, 0}};
}

struct CacheFixture {
  Console &console;
  HardwareRenderer &renderer;
  Cpu &cpu;

  CacheFixture(Console &console, HardwareRenderer &renderer)
      : console(console), renderer(renderer), cpu(console.cpu()) {
    renderer.synchronize();
    const std::uint32_t registers[][2] = {{0, 0x302},
                                          {1, 0x180000},
                                          {2, 64},
                                          {6, 525},
                                          {7, 3093},
                                          {8, 0x0c150c15},
                                          {9, (108u << 16) | 748},
                                          {10, (34u << 16) | 514},
                                          {12, 102},
                                          {13, 273}};
    for (const auto &reg : registers)
      renderer.write_video(reg[0], reg[1]);
  }

  void reset() {
    cpu.power();
    cpu.write_control(Status, 0x30000000);
    cpu.write_control(Compare, 0xffffffff);
    cpu.state().gpr[5] = 0x30000000;
    console.display().power();
  }

  std::uint64_t load(std::uint64_t address) {
    cpu.state().gpr[30] = address;
    cpu.execute(i(35, 30, 1, 0));
    return cpu.state().gpr[1];
  }

  void store(std::uint64_t address, std::uint32_t value) {
    cpu.state().gpr[30] = address;
    cpu.state().gpr[1] = value;
    cpu.execute(i(43, 30, 1, 0));
  }

  void maintain(unsigned operation, std::uint64_t address) {
    cpu.state().gpr[30] = address;
    if (operation == 8)
      cpu.write_control(TagLo, 0);
    cpu.execute(i(47, 30, operation, 0));
  }

  void execute(bool native, std::uint64_t address, std::uint64_t end) {
    cpu.set_pc(address);
    for (unsigned pass = 0; pass < 32 && cpu.state().pc != end; ++pass) {
      const auto budget = cpu.state().clocks + 4096;
      if (!native || !cpu.run_block(budget))
        cpu.step();
    }
    equal(cpu.state().pc, end);
    equal(cpu.read_control(Cause) & 0x7c, 0);
  }

  void submit(std::vector<Packet> packets, bool sync) {
    if (sync)
      packets.push_back({0x29000000, 0});
    for (unsigned n = 0; n < packets.size(); ++n) {
      console.ram().write(0x2000 + n * 8, 4, packets[n][0]);
      console.ram().write(0x2004 + n * 8, 4, packets[n][1]);
    }
    console.display().write_word(0, 0x2000);
    console.display().write_word(4, 0x2000 + packets.size() * 8);
    equal(renderer.crashed(), false);
  }

  std::uint64_t read_frame() {
    const auto frame = renderer.read_frame();
    equal(frame.width, 640);
    equal(frame.height, 240);
    equal(frame.rgba.size(), std::size_t(frame.width) * frame.height * 4);
    std::uint64_t hash = 14695981039346656037ull;
    for (auto value : frame.rgba)
      hash = (hash ^ value) * 1099511628211ull;
    return hash;
  }

  void begin_frame() {
    renderer.begin_frame(false);
  }
};

} // namespace test::renderer_cache
