#pragma once
#include "../commands/wire_scenarios.hpp"

namespace test::pak_bank {
template <class Probe> void scenarios(Probe &p) {
  for (unsigned banks : {1u, 2u, 4u, 16u, 62u})
    for (bool patterned : {false, true})
      for (unsigned requested : {0u, 1u, 3u, 15u, 61u, 62u, 255u}) {
        p.reset(false);
        p.connect_bank(banks, patterned);

        p.snapshot();
        test::peripheral_wire::command(p, 0, 0, 1, 3, true);
        std::array<std::uint8_t, 4> bank_command{3, 0x80,
                                                 static_cast<std::uint8_t>(p.address_crc(0x8000)),
                                                 static_cast<std::uint8_t>(requested)};
        p.command(bank_command, 1);
        p.observe();
        test::peripheral_wire::command(p, 2, 0x20, 3, 33, true);
        test::peripheral_wire::command(p, 3, 0x20, 35, 1, true, 0x84);
        test::peripheral_wire::command(p, 2, 0x20, 3, 33, true);
        p.snapshot();
        p.power(true);
        test::peripheral_wire::command(p, 2, 0x20, 3, 33, true);
        p.snapshot();
        p.disconnect();
        test::peripheral_wire::command(p, 0, 0, 1, 3, true);
        p.snapshot();
        p.finish();
      }
}
} // namespace test::pak_bank
