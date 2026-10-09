#include "assertions.hpp"
#include "rdp_expected.hpp"

namespace test {

void gpu_crash_reset_tests() {
  using namespace renderer_replay;
  GpuFixture fixture;
  unsigned id = 0, snapshot = 0;
  for (unsigned kind = 0; kind < 7; ++kind)
    for (unsigned route : {0u, 1u, 2u})
      for (bool sync : {false, true})
        for (bool warm : {false, true}) {
          fixture.power(false);
          RenderCase input{"crash-reset", kind == 5 ? 3u : 2u};
          input.opcode = kind;
          input.load = warm;
          auto check = [&](unsigned stage) {
            const auto &result = expected::reset[snapshot++];
            return capture(fixture, {input, id, stage, route, sync}, result.memory, result.pixels,
                           result.dpc, result.irq, result.crashed);
          };
          initialize_render(fixture, input);
          if (!check(0))
            return;
          std::vector<RdpPacket> packets = {{0x3f00000f | (input.framebuffer << 19), color_address},
                                            {0x3e000000, depth_address},
                                            {0x2d000000, 0x00040040}};
          if (kind == 0) {
            packets.push_back({0x3d400007, texture_address});
            packets.push_back(tile(7, {2, 0}, 1, 0));
            packets.push_back({0x34000000, 0x07000000 | (28u << 12) | 28});
          } else if (kind == 6) {
            packets.push_back({0x3d100003, palette_address});
            packets.push_back(tile(7, {0, 2}, 0, 256));
            packets.push_back({0x30000000, 0x07000000 | (12u << 12) | 4});
          } else if (kind == 5) {
            unsigned start = 0;
            auto copy = input;
            copy.opcode = 8;
            copy.rectangle = true;
            copy.cycle = 2;
            packets = render_commands(copy, start);
          } else {
            if (kind == 1)
              packets.push_back({0x3f80000f, color_address});
            packets.push_back({0x2f300000, kind == 2   ? 16u
                                           : kind == 3 ? 64u
                                           : kind == 4 ? 32u
                                                       : 0u});
            packets.push_back({0x37000000, 0xf801f801});
            packets.push_back({0x3602802c, 0x00004004});
          }
          if (sync)
            packets.push_back({0x29000000, 0});
          const unsigned base = route == 1 ? 0x1ff8 : 0x2000;
          upload(fixture, packets, base, route == 1);
          if (route == 1)
            fixture.display_write(3, 2);
          if (route == 2)
            fixture.display_write(3, 8);
          fixture.display_write(0, base);
          fixture.display_write(1, base + static_cast<unsigned>(packets.size()) * 8);
          if (route == 2)
            fixture.display_write(3, 4);
          fixture.console.display().advance(17);
          if (!check(1))
            return;
          fixture.display_write(3, 4 | 64 | 128 | 256);
          fixture.command_word(base, 0x29000000, route == 1);
          fixture.command_word(base + 4, 0, route == 1);
          fixture.display_write(0, base);
          fixture.display_write(1, base + 8);
          fixture.console.display().advance(31);
          if (!check(2))
            return;
          fixture.power(warm);
          auto video = base_video(input.framebuffer);
          video[2] = 16;
          video[12] = 25;
          video[13] = 68;
          fixture.program(video);
          if (!check(3))
            return;
          auto recover = input;
          recover.opcode = 8;
          recover.load = 0;
          initialize_render(fixture, recover);
          unsigned start = 0;
          packets = render_commands(recover, start);
          packets.push_back({0x29000000, 0});
          upload(fixture, packets, 0x2000, false);
          fixture.display_write(0, 0x2000);
          fixture.display_write(1, 0x2000 + static_cast<unsigned>(packets.size()) * 8);
          fixture.console.display().advance(31);
          if (!check(4))
            return;
          ++id;
        }
  equal(snapshot, std::size(expected::reset));
  std::cout << "RDP crash/reset replay: " << id << " scenes, " << snapshot << " captures\n";
}

} // namespace test
