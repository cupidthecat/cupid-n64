#pragma once
#include "../../system/cpu/fixture.hpp"
#include "program.hpp"

namespace test::save_cpu {
struct Machine : cpu_bus::Machine {
  void reset(unsigned size, bool compiled) {
    cupid::n64::ConsoleConfig config;
    config.sram_size = size;
    create(config, compiled);
    load_rom();
  }
  void reset_joy(unsigned size, bool present, bool compiled) {
    cupid::n64::ConsoleConfig config;
    config.eeprom_size = size;
    config.rtc_present = present;
    config.rtc_clock = [] { return 1700000000; };
    create(config, compiled);
    if (present && !console->rtc().load(ClockStorage))
      throw std::runtime_error("CPU save RTC storage load failed");
    load_rom();
  }
  void fill(unsigned size) {
    fill_ram();
    for (unsigned n = 0; n < size; ++n)
      console->sram().data()[n] = save_cpu::pattern(n, 0x29);
  }
  std::uint64_t save_word(unsigned address) {
    std::uint64_t value = 0;
    for (unsigned n = 0; n < 8; ++n)
      value = value << 8 | console->sram().data()[address + n];
    return value;
  }
  void boot() {
    auto &p = console->pif();
    p.tick();
    p.write_word(0x1fc007fc, 0x10);
    p.write_word(0x1fc007f0, 0xa536);
    p.write_word(0x1fc007f4, 0xc0f1d859);
    p.write_word(0x1fc007fc, 0x20);
    p.write_word(0x1fc007fc, 0x40);
    p.write_word(0x1fc007fc, 8);
    if (p.state() != Pif::State::Run)
      throw std::runtime_error("Save Joybus boot failed");
  }
  void fill_joy(unsigned size) {
    fill_ram();
    for (unsigned n = 0; n < size; ++n)
      console->eeprom().data()[n] = save_cpu::pattern(n, 0x29);
  }
  unsigned pif_state() {
    return static_cast<unsigned>(console->pif().state());
  }
  bool pif_reset() {
    return console->pif().reset_enabled();
  }
  bool eeprom_busy() {
    std::array<std::uint8_t, 3> out{};
    console->eeprom().communicate(std::array<std::uint8_t, 1>{0}, out);
    return out[2] != 0;
  }
  bool rtc_present() {
    return console->rtc().present();
  }
  unsigned rtc_status() {
    std::array<std::uint8_t, 3> out{};
    console->rtc().communicate(std::array<std::uint8_t, 1>{6}, out);
    return out[2];
  }
  std::uint64_t pif_word(unsigned address) {
    std::uint64_t value = 0;
    for (unsigned n = 0; n < 8; ++n)
      value = value << 8 | console->pif().ram()[address + n];
    return value;
  }
  std::uint64_t eeprom_word(unsigned address) {
    std::uint64_t value = 0;
    for (unsigned n = 0; n < 8; ++n)
      value = value << 8 | console->eeprom().data()[address + n];
    return value;
  }
  std::uint64_t rtc_word(unsigned address) {
    std::array<std::uint8_t, 9> out{};
    console->rtc().communicate(
        std::array<std::uint8_t, 2>{7, static_cast<std::uint8_t>(address / 8)}, out);
    std::uint64_t value = 0;
    for (unsigned n = 0; n < 8; ++n)
      value = value << 8 | out[n];
    return value;
  }
};
} // namespace test::save_cpu
