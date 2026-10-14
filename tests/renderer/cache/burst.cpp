#include "../../support/test.hpp"
#include "core/system/console.hpp"
#include "renderer/vulkan/renderer.hpp"
#include <array>

namespace test {
using namespace cupid::n64;

void gpu_burst_coverage_tests(Console &console, HardwareRenderer &renderer) {
  constexpr std::uint32_t origin = 0x180000;
  std::array<std::uint32_t, 64 * 64 / 2> words;
  const auto pixel = [](unsigned n) {
    const auto x = n % 64, y = n / 64;
    return ((x / 2) << 11) | ((y / 2) << 6) | (((x ^ y) / 2) << 1) | (((x * 7 + y * 13) & 3) != 0);
  };
  renderer.synchronize();
  for (unsigned n = 0; n < words.size(); ++n) {
    words[n] = (pixel(n * 2) << 16) | pixel(n * 2 + 1);
    console.ram().write(origin + n * 4, 4, words[n]);
  }
  const std::uint32_t registers[][2] = {{0, 0x002},
                                        {1, origin},
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
  const auto scalar = renderer.frame(false);
  equal(scalar.width, 640);
  equal(scalar.height, 240);
  equal(scalar.rgba.empty(), false);
  auto hidden = console.ram().hidden().subspan(origin / 2, words.size() * 2);
  std::fill(hidden.begin(), hidden.end(), 0);
  equal(renderer.frame(false).rgba != scalar.rgba, true);

  for (unsigned count : {4u, 8u, 0u}) {
    for (unsigned n = 0; n < words.size(); ++n)
      console.ram().write(origin + n * 4, 4, 0);
    auto &cpu = console.cpu();
    cpu.power();
    cpu.write_control(Status, 0x30000000);
    const auto stride = count ? count : 4;
    for (unsigned n = 0; n < words.size(); n += stride) {
      const auto address = origin + n * 4;
      if (count) {
        console.ram().write_burst(address, std::span(words).subspan(n, count),
                                  MemoryRequestor::DataCache);
      } else {
        cpu.state().gpr[30] = 0xffffffff80000000ull + address;
        for (unsigned word = 0; word < 4; ++word) {
          cpu.state().gpr[1] = words[n + word];
          cpu.execute(i(43, 30, 1, static_cast<std::uint16_t>(word * 4)));
          equal(console.ram().read(address + word * 4, 4), 0);
        }
        cpu.execute(i(47, 30, 21, 0));
      }
    }
    equal(cpu.read_control(Cause) & 0x7c, 0);
    for (unsigned n = 0; n < words.size(); ++n) {
      equal(console.ram().read(origin + n * 4, 4), words[n]);
      equal(hidden[n * 2], ((words[n] >> 16) & 1) * 3);
      equal(hidden[n * 2 + 1], (words[n] & 1) * 3);
    }
    equal(renderer.frame(false).rgba == scalar.rgba, true);
    equal(renderer.crashed(), false);
  }
}

} // namespace test
