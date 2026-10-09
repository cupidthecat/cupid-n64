#include "assertions.hpp"
#include "rdp_expected.hpp"

namespace test {

void gpu_command_replay_tests() {
  using namespace renderer_replay;
  GpuFixture fixture;
  unsigned id = 0;
  for (const auto &input : render_cases())
    for (unsigned route : {0u, 1u, 2u})
      for (bool sync : {false, true}) {
        fixture.reset_commands();
        initialize_render(fixture, input);
        unsigned draw_start = 0;
        auto packets = render_commands(input, draw_start);
        if (sync)
          packets.push_back({0x29000000, 0});
        const unsigned base = route == 1 ? 0x1ff8 : 0x2000;
        const unsigned source = route == 1, frozen = route == 2;
        const unsigned prefix = base + (draw_start + (input.opcode == 0x36 ? 0 : 1)) * 8;
        const unsigned end = base + static_cast<unsigned>(packets.size()) * 8;
        upload(fixture, packets, base, source);
        if (source)
          fixture.display_write(3, 2);
        if (frozen)
          fixture.display_write(3, 8);
        fixture.display_write(0, base);
        const auto &blank = expected::blank[input.framebuffer - 2];
        if (!capture(fixture, {input, id, 0, route, sync}, blank.memory, blank.pixels,
                     {base, 0, 0, 1152 + source + frozen * 2, 0, 0, 0, 0}, 23, false))
          return;
        fixture.display_write(1, prefix);
        fixture.console.display().advance(17);
        const bool partial = !frozen && input.opcode != 0x36;
        equal(fixture.console.display().buffered_words(), partial ? draw_start + 1 : 0);
        equal(fixture.console.display().consumed_words(), partial ? draw_start : 0);
        const unsigned partial_start = frozen || input.opcode == 0x36 ? base : prefix;
        if (!capture(fixture, {input, id, 1, route, sync}, blank.memory, blank.pixels,
                     {partial_start, prefix, frozen ? base : prefix,
                      128 + source + (frozen ? 2u : 40u), 6, 0, frozen ? 0u : 1u, 0},
                     23, false))
          return;
        fixture.display_write(1, end);
        if (frozen)
          fixture.display_write(3, 4);
        fixture.console.display().advance(31);
        equal(fixture.console.display().buffered_words(), 0);
        equal(fixture.console.display().consumed_words(), 0);
        const auto &rendered = expected::rendered[id];
        if (!capture(fixture, {input, id, 2, route, sync}, rendered.memory, rendered.pixels,
                     {partial_start, end, end, 128 + source + (sync ? 0u : 40u), 16, 0,
                      sync ? 0u : 1u, 0},
                     sync ? 55 : 23, false))
          return;
        ++id;
      }
  equal(id, std::size(expected::rendered));
  std::cout << "RDP command replay: " << id << " scenes, " << id * 3 << " captures\n";
}

} // namespace test
