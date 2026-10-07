#pragma once

#include "core/controller/accessories/cartridge/handheld.hpp"
#include <array>
#include <vector>

namespace cupid::n64 {

struct HandheldCartridge::Implementation {
  HostClock clock;
  Config config;
  std::vector<std::uint8_t> rom, ram, flash, eeprom;
  unsigned rom_size = 0;
  unsigned rom_bank = 1;
  unsigned ram_bank = 0;
  unsigned base_bank = 0;
  unsigned mode = 0;
  bool ram_enabled = false;
  bool extra_enabled = false;
  bool rumble = false;
  bool board_present = false;
  std::array<unsigned, 2> region_bank{};
  std::array<unsigned, 2> ram_region{};
  std::array<bool, 2> region_flash{};
  bool flash_enabled = false;
  bool flash_writable = false;
  std::array<std::uint8_t, 5> time{};
  std::array<std::uint8_t, 5> latched{};
  bool latch = false;
  std::int16_t motion_x = 0, motion_y = 0;
  std::uint16_t tilt_x = 0x81d0, tilt_y = 0x81d0;
  unsigned select = 0, input = 0, output = 0, index = 0;
  bool ready = false;
  unsigned time_index = 0;
  std::array<std::uint8_t, 7> calendar{};
  bool serial_select = false, serial_clock = false, serial_writable = false;
  std::uint64_t serial_input = 0;
  unsigned serial_bits = 0, serial_output = 0, serial_remaining = 0, serial_address = 0;
  unsigned serial_busy = 0;
  unsigned serial_mode = 0;

  static std::uint8_t memory(const std::vector<std::uint8_t> &data, unsigned address);
  static void store(std::vector<std::uint8_t> &data, unsigned address, std::uint8_t value);
  std::uint8_t read(std::uint16_t address);
  void write(std::uint16_t address, std::uint8_t value);
  std::uint8_t tama_read(std::uint16_t address);
  void tama_write(std::uint16_t address, std::uint8_t value);
  std::uint8_t serial_read() const;
  void serial_write(std::uint8_t value);
  void serial_reset();
  void initialize_clock();
  bool load_clock(std::span<const std::uint8_t> data);
  std::vector<std::uint8_t> save_clock() const;
};

} // namespace cupid::n64
