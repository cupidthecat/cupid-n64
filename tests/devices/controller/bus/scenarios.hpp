#pragma once

#include "../../system/reset/immediate/scenarios.hpp"

namespace test::peripheral_bus {
constexpr unsigned PacketAddress = 0x2000;
constexpr unsigned ReplyAddress = 0x3000;
constexpr std::uint8_t pattern(unsigned address) {
  return static_cast<std::uint8_t>(address * 31 + (address >> 7) + 0x53);
}

template <class Probe> void observe(Probe &p) {
  for (unsigned offset = 0; offset < 32; offset += 4)
    p.word(p.read(0x04800000 + offset));
  p.word(p.read(0x04300008));
  p.word(p.device != 4 && p.motor());
  for (unsigned n = 0; n < 64; n += 8)
    p.word(p.pif_memory(n));
  for (unsigned n = 0; n < 64; n += 8)
    p.word(p.raw_memory(ReplyAddress + n));
}

template <class Probe> void transfer(Probe &p, bool read) {
  p.write(0x04800000, read ? ReplyAddress : PacketAddress);
  p.write(read ? 0x04800004 : 0x04800010, 0x1fc007c0);
  test::peripheral_bus::observe(p);
  const auto deadline = read ? p.timing() * 3 : 12195;
  p.advance(deadline - 1);
  test::peripheral_bus::observe(p);
  p.advance(1);
  test::peripheral_bus::observe(p);
  p.write(0x04800018, 0xffffffff);
}

template <class Probe>
void command(Probe &p, unsigned channel, unsigned padding, unsigned flags, unsigned opcode,
             unsigned address, unsigned send, unsigned receive, bool valid_crc,
             unsigned value = 0) {
  std::array<std::uint8_t, 64> packet;
  packet.fill(0xa5);
  for (unsigned n = 0; n < channel; ++n)
    packet[n] = 0;
  for (unsigned n = channel; n < channel + padding; ++n)
    packet[n] = 0xff;
  const auto start = channel + padding;
  packet[start] = static_cast<std::uint8_t>(send | flags);
  packet[start + 1] = static_cast<std::uint8_t>(receive);
  packet[start + 2] = static_cast<std::uint8_t>(opcode);
  if (send > 1)
    packet[start + 3] = static_cast<std::uint8_t>(address >> 8);
  if (send > 2)
    packet[start + 4] =
        static_cast<std::uint8_t>((address & 0xe0) | (p.address_crc(address) ^ !valid_crc));
  for (unsigned n = 3; n < send; ++n)
    packet[start + 2 + n] =
        static_cast<std::uint8_t>(value ? value : test::peripheral_bus::pattern(n));
  packet[start + 2 + send + receive] = 0xfe;
  packet[63] = 1;
  p.packet(packet);
  transfer(p, false);
  transfer(p, true);
}

template <class Probe> void scenarios(Probe &p) {
  for (unsigned device = 0; device < 5u; ++device)
    for (unsigned channel : {0u, 3u})
      for (unsigned padding : {0u, 7u})
        for (unsigned epoch : {0u, 0xfffffff0u})
          for (unsigned address : {0x20u, 0x8000u, 0xc000u})
            for (bool valid_crc : {false, true}) {
              p.reset(false);
              test::machine_reset::initialize(p);
              p.boot();
              p.channel = channel;
              p.connect(device);
              p.set_epoch(epoch);
              p.write(0x04800018, 0xffffffff);

              command(p, channel, padding, 0, 0, 0, 1, 3, true);
              command(p, channel, padding, 0, 2, address, 3, 33, valid_crc);
              command(p, channel, padding, 0, 3, address, 4, 1, valid_crc, 1);
              command(p, channel, padding, 0, 2, address, 3, 33, valid_crc);
              command(p, channel, padding, 0, 3, address, 35, 1, valid_crc, 0x84);
              command(p, channel, padding, 0, 2, address, 3, 33, valid_crc);
              command(p, channel, padding, 0, 1, 0, 1, 4, true);
              command(p, channel, padding, 0, 3, address, 3, 1, valid_crc);
              command(p, channel, padding, 0x80, 2, address, 3, 33, valid_crc);
              command(p, channel, padding, 0x40, 2, address, 3, 33, valid_crc);
              command(p, channel, padding, 0, 0, 0, 1, 3, true);
              p.disconnect();
              command(p, channel, padding, 0, 0, 0, 1, 3, true);
              p.power(true);
              test::machine_reset::initialize(p);
              p.boot();
              p.set_epoch(0);
              p.write(0x04800018, 0xffffffff);
              command(p, channel, padding, 0, 0, 0, 1, 3, true);
              p.finish();
            }
}
} // namespace test::peripheral_bus
