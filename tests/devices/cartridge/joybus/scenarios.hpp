#pragma once
#include "../cpu/program.hpp"
#include <algorithm>
#include <span>

namespace test::cpu_save_joybus {
constexpr unsigned Packet = 0x1000, Reply = 0x2000;
constexpr unsigned WriteClocks = 1125000, Second = 187500000;
template <class P> void wait(P &p) {
  for (unsigned call = 0; call < 10000; ++call) {
    if (!(p.access(false, 0x04800018, 0) & 3))
      return;
    p.idle(512);
  }
  throw std::runtime_error("CPU save Joybus DMA did not finish");
}
template <class P> void prepare(P &p, std::span<const std::uint8_t> input, unsigned receive) {
  std::array<std::uint8_t, 64> packet{};
  packet.fill(0xa5);
  for (unsigned n = 0; n < 4; ++n)
    packet[n] = 0;
  packet[4] = static_cast<std::uint8_t>(input.size());
  packet[5] = static_cast<std::uint8_t>(receive);
  std::copy(input.begin(), input.end(), packet.begin() + 6);
  packet[6 + input.size() + receive] = 0xfe;
  packet[63] = 1;
  for (unsigned n = 0; n < packet.size(); n += 4) {
    const auto value = unsigned(packet[n]) << 24 | unsigned(packet[n + 1]) << 16 |
                       unsigned(packet[n + 2]) << 8 | packet[n + 3];
    p.access(true, Packet + n, value);
  }
  p.access(true, 0x04800000, Packet);
  p.access(true, 0x04800010, 0x1fc007c0);
  wait(p);
  p.access(true, 0x04800018, 0);
}
template <class P> void start_read(P &p) {
  p.access(true, 0x04800000, Reply);
  p.access(true, 0x04800004, 0x1fc007c0);
}
template <class P> void transact(P &p, std::span<const std::uint8_t> input, unsigned receive) {
  prepare(p, input, receive);
  start_read(p);
  wait(p);
  for (unsigned n = 0; n < 64; n += 4)
    p.word(p.access(false, Reply + n, 0));
  p.access(true, 0x04800018, 0);
}
template <class P> void observe(P &p, unsigned phase, unsigned size) {
  p.word(phase);
  p.word(p.clock());
  p.word(p.pc());
  for (unsigned n = 0; n < 32; ++n)
    p.word(p.gpr(n));
  for (unsigned n : {9u, 11u, 12u, 13u, 16u, 30u})
    p.word(p.control(n));
  p.word(p.frozen());
  for (unsigned n = 0; n < 8; ++n)
    p.word(p.register_word(0x04800000 + n * 4));
  p.word(p.register_word(0x04300008));
  p.word(p.register_word(0x0430000c));
  p.word(p.pif_state());
  p.word(p.pif_reset());
  p.word(p.eeprom_busy());
  p.word(p.rtc_present());
  p.word(p.rtc_status());
  p.word(p.deadline());
  for (unsigned n = 0; n < 64; n += 8)
    p.word(p.pif_word(n));
  for (unsigned n = 0; n < save_cpu::RamBytes; n += 8)
    p.word(p.ram_word(n));
  for (unsigned n = 0; n < save_cpu::RamBytes / 2; n += 8)
    p.word(p.coverage_word(n));
  for (unsigned n = 0; n < size; n += 8)
    p.word(p.eeprom_word(n));
  for (unsigned n = 0; n < 32; n += 8)
    p.word(p.rtc_word(n));
}
template <class P> void initialize(P &p) {
  save_cpu::initialize_memory(p);
  p.boot();
  p.access(true, 0x0430000c, 8);
}
template <class P> void eeprom(P &p) {
  for (unsigned size : {0u, 512u, 2048u})
    for (bool present : {false, true})
      for (bool native : {false, true})
        for (bool warm : {false, true})
          for (unsigned variant = 0; variant < 4; ++variant)
            for (unsigned boundary = 0; boundary < 3; ++boundary) {
              p.reset_joy(size, present, native);
              initialize(p);
              p.fill_joy(size);
              observe(p, 0, size);
              std::array<std::uint8_t, 18> write{5, 3};
              write[1] = std::array<std::uint8_t, 4>{3, 255, 63, 127}[variant];
              for (unsigned n = 2; n < write.size(); ++n)
                write[n] = save_cpu::pattern(n, variant);
              const auto length = std::array<unsigned, 4>{10, 18, 2, 1}[variant];
              transact(p, std::span(write).first(length), 1);
              observe(p, 1, size);
              const auto remaining = p.eeprom_busy() ? p.deadline() : WriteClocks;
              if (!remaining || remaining > WriteClocks)
                throw std::runtime_error("EEPROM deadline unavailable");
              p.idle(remaining - 1);
              observe(p, 2, size);
              p.idle(boundary);
              observe(p, 3, size);
              p.word(boundary ? p.access(false, 0x04800018, 0) : 0xfeed);
              observe(p, 4, size);
              p.power(warm);
              observe(p, 5, size);
              initialize(p);
              transact(p, std::array<std::uint8_t, 1>{0}, 3);
              observe(p, 6, size);
              write[2] ^= 0xff;
              transact(p, write, 1);
              observe(p, 7, size);
              p.idle(WriteClocks * 2);
              transact(p, std::array<std::uint8_t, 2>{4, write[1]}, 16);
              transact(p, std::array<std::uint8_t, 1>{255}, 3);
              observe(p, 8, size);
              p.finish();
            }
}
template <class P> void rtc(P &p) {
  for (unsigned size : {0u, 512u, 2048u})
    for (bool present : {false, true})
      for (bool native : {false, true})
        for (bool warm : {false, true})
          for (unsigned variant = 0; variant < 4; ++variant)
            for (unsigned boundary = 0; boundary < 3; ++boundary) {
              p.reset_joy(size, present, native);
              initialize(p);
              p.fill_joy(size);
              observe(p, 0, size);
              std::array<std::uint8_t, 10> control{8, 0};
              if (variant == 1 || variant == 2)
                control[3] = 4;
              if (variant == 3)
                control[2] = 3;
              transact(p, control, 1);
              if (variant == 1) {
                p.idle(Second / 2);
                control[3] = 0;
                transact(p, control, 1);
              }
              observe(p, 1, size);
              const auto remaining = present ? p.deadline() : Second;
              if (!remaining || remaining > Second)
                throw std::runtime_error("RTC deadline unavailable");
              p.idle(remaining - 1);
              observe(p, 2, size);
              p.idle(boundary);
              observe(p, 3, size);
              p.word(boundary ? p.access(false, 0x04800018, 0) : 0xfeed);
              observe(p, 4, size);
              p.power(warm);
              observe(p, 5, size);
              initialize(p);
              transact(p, std::array<std::uint8_t, 2>{7, 2}, 9);
              observe(p, 6, size);
              std::array<std::uint8_t, 10> date{8, 2, 0x59, 0x59, 0xa3, 0x28, 3, 2, 0, 1};
              transact(p, std::span(date).first(variant == 2 ? 9 : 10), 1);
              observe(p, 7, size);
              p.idle(Second);
              transact(p, std::array<std::uint8_t, 2>{7, 2}, 9);
              transact(p, std::array<std::uint8_t, 1>{6}, 3);
              observe(p, 8, size);
              p.finish();
            }
}
} // namespace test::cpu_save_joybus
