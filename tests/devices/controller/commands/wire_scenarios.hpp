#pragma once

#include "../../system/reset/immediate/scenarios.hpp"
#include <array>
#include <cstdint>
#include <span>

namespace test::peripheral_wire {
constexpr std::uint8_t pattern(unsigned address) {
  return static_cast<std::uint8_t>(address * 31 + (address >> 7) + 0x53);
}
template <class Probe>
void command(Probe &p, unsigned opcode, unsigned address, unsigned send, unsigned receive,
             bool valid_crc, unsigned value = 0) {
  std::array<std::uint8_t, 64> input{};
  input[0] = static_cast<std::uint8_t>(opcode);
  input[1] = static_cast<std::uint8_t>(address >> 8);
  input[2] = static_cast<std::uint8_t>((address & 0xe0) | (p.address_crc(address) ^ !valid_crc));
  for (unsigned n = 3; n < input.size(); ++n)
    input[n] = static_cast<std::uint8_t>(value ? value : pattern(n));
  p.command(std::span(input).first(send), receive);
  p.observe();
}
template <class Probe> void scenarios(Probe &p) {
  for (unsigned device = 0; device < 5u; ++device)
    for (unsigned address :
         {0u, 0x20u, 0x7fe0u, 0x8000u, 0x8fe0u, 0x9000u, 0xa000u, 0xb000u, 0xc000u, 0xffe0u})
      for (unsigned receive : {0u, 1u, 2u, 3u, 4u, 31u, 32u, 33u, 34u, 63u})
        for (bool valid_crc : {false, true}) {
          p.reset(false);
          p.connect(device);

          test::peripheral_wire::command(p, 2, address, 3, receive, valid_crc);
          test::peripheral_wire::command(p, 0, 0, 1, 3, true);
          test::peripheral_wire::command(p, 2, address, 3, receive, valid_crc);
          test::peripheral_wire::command(p, 255, 0, 1, 3, true);
          test::peripheral_wire::command(p, 3, address, 4, 1, valid_crc, 1);
          test::peripheral_wire::command(p, 2, address, 3, receive, valid_crc);
          test::peripheral_wire::command(p, 3, address, 35, 1, valid_crc, 0x84);
          test::peripheral_wire::command(p, 2, address, 3, receive, valid_crc);
          test::peripheral_wire::command(p, 1, 0, 1, receive, true);
          test::peripheral_wire::command(p, 3, address, 3, 1, valid_crc);
          test::peripheral_wire::command(p, 2, address, 2, receive, valid_crc);
          test::peripheral_wire::command(p, 4, address, 1, receive, valid_crc);
          p.disconnect();
          test::peripheral_wire::command(p, 0, 0, 1, 3, true);
          test::peripheral_wire::command(p, 2, address, 3, receive, valid_crc);
          p.power(true);
          test::peripheral_wire::command(p, 0, 0, 1, 3, true);
          p.finish();
        }
}
} // namespace test::peripheral_wire
