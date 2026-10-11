#include "../replay/commands.hpp"
#include "../replay/fixture.hpp"

namespace test {

void gpu_primitive_lod_tests() {
  using namespace renderer_replay;
  GpuFixture fixture;
  constexpr RdpPacket combine = {0x3c000000 | (6u << 20) | (14u << 15) | (7u << 12) | (7u << 9) |
                                     (6u << 5) | 14,
                                 (8u << 28) | (8u << 24) | (7u << 21) | (7u << 18) | (7u << 15) |
                                     (7u << 12) | (3u << 9) | (7u << 6) | (7u << 3) | 3};
  for (unsigned cycle : {0u, 1u})
    for (unsigned fraction = 0; fraction < 256; ++fraction) {
      fixture.reset_commands();
      for (unsigned n = 0; n < 16 * 16; ++n)
        fixture.put(color_address + n * 4, 0x12345678);
      const RdpPacket packets[] = {{0x3f18000f, color_address},
                                   {0x2d000000, 0x00040040},
                                   {0x2f0000f0 | (cycle << 20), 0},
                                   {0x3a000000 | fraction, 0xffffffff},
                                   combine,
                                   {0x36040040, 0},
                                   {0x29000000, 0}};
      for (const auto &packet : packets)
        fixture.renderer->submit(packet);
      fixture.renderer->synchronize();
      equal(fixture.crashed(), false);
      for (unsigned y = 1; y < 15; ++y)
        for (unsigned x = 1; x < 15; ++x) {
          const auto pixel = fixture.console.ram().read(color_address + (y * 16 + x) * 4, 4);
          equal(pixel >> 8, fraction * 0x010101u);
        }
    }
}

} // namespace test
