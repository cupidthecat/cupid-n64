#pragma once
#include <array>
#include <cstdint>
namespace test::gamecube {
template <class Probe> void route_scenarios(Probe &p) {
  const std::array<std::uint8_t, 3> read{0x40, 3, 1};
  const std::array<std::uint8_t, 1> id{0}, reset_id{0xff}, origin{0x41}, calibration{0x42},
      long_read{0x43}, invalid{1};
  for (unsigned port = 0; port < 4; ++port) {
    p.select(port, 3);
    p.command(port, id, 3);
    for (unsigned mode = 0; mode < 256; ++mode) {
      const std::array<std::uint8_t, 3> cmd{0x40, static_cast<std::uint8_t>(mode),
                                            static_cast<std::uint8_t>(mode)};
      p.command(port, cmd, 8);
      p.command(port, reset_id, 3);
    }
    for (unsigned size = 0; size <= 10; ++size) {
      p.reset(port);
      p.command(port, read, 8);
      p.command(port, origin, size);
      p.command(port, long_read, 10);
      p.command(port, calibration, size);
      p.command(port, long_read, size);
    }
    p.command(port, invalid, 4);
    p.command(port, long_read, 10, 0x80);
    p.command(port, long_read, 10, 0x40);
    p.command(port, long_read, 10);
    p.command(port, read, 8);
    p.short_reset(port);
    p.command(port, long_read, 10);
    p.command(port, read, 8);
    p.power(true);
    p.command(port, reset_id, 3);
    p.command(port, long_read, 10);
    p.power(false);
    p.command(port, reset_id, 3);
    p.command(port, long_read, 10);
    p.select(port, 0);
    p.command(port, read, 8);
    p.select(port, 1);
    p.command(port, id, 3);
    p.select(port, 2);
    p.command(port, id, 3);
    p.select(port, 3);
    p.command(port, long_read, 10);
    p.finish();
  }
}
} // namespace test::gamecube
