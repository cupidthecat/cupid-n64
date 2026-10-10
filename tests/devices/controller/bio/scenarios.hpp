#pragma once
#include "../commands/wire_scenarios.hpp"

namespace test::bio_clock {
template <class Probe> void scenarios(Probe &p) {
  for (unsigned bpm : {30u, 60u, 90u, 120u, 180u})
    for (std::uint64_t epoch : {0ull, 0xfffffff0ull, 0xfffffffffffc0000ull})
      for (unsigned address : {0u, 0x7fe0u, 0x8000u, 0xbfe0u, 0xc000u, 0xffe0u})
        for (unsigned receive : {1u, 3u, 32u, 33u, 63u}) {
          p.reset(false);
          p.clock(epoch);
          p.connect_sensor(bpm);

          test::peripheral_wire::command(p, 0, 0, 1, 3, true);
          const auto interval = 60000000 / bpm;
          for (std::uint64_t elapsed : std::array<std::uint64_t, 8>{
                   0, 199999, 200000, interval - 1, interval, interval + 199999, interval + 200000,
                   interval * 4 + 17}) {
            p.clock(epoch + elapsed);
            test::peripheral_wire::command(p, 2, address, 3, receive, true);
          }
          test::peripheral_wire::command(p, 3, address, 4, 1, true, 0x84);
          test::peripheral_wire::command(p, 2, address, 3, receive, true);
          p.power(true);
          test::peripheral_wire::command(p, 2, address, 3, receive, true);
          p.disconnect();
          test::peripheral_wire::command(p, 0, 0, 1, 3, true);
          test::peripheral_wire::command(p, 2, address, 3, receive, true);
          p.finish();
        }
}
} // namespace test::bio_clock
