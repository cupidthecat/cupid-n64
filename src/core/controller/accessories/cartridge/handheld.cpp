#include "identity.hpp"
#include "implementation.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <string>

namespace cupid::n64 {

HandheldCartridge::HandheldCartridge(HostClock clock)
    : implementation_(std::make_unique<Implementation>()) {
  implementation_->clock = std::move(clock);
  if (!implementation_->clock)
    implementation_->clock = [] {
      return std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch())
          .count();
    };
}

HandheldCartridge::~HandheldCartridge() = default;

bool HandheldCartridge::load(std::span<const std::uint8_t> rom) {
  return load(rom, Config{});
}

bool HandheldCartridge::load(std::span<const std::uint8_t> rom, Config config) {
  if (rom.size() < 0x4000 || rom.size() > 0x1000000)
    return false;
  unsigned header = 0;
  if (rom.size() >= 0x8000) {
    const auto candidate = static_cast<unsigned>(rom.size() - 0x8000);
    if (rom[candidate + 0x104] == 0xce && rom[candidate + 0x105] == 0xed &&
        rom[candidate + 0x106] == 0x66 && rom[candidate + 0x107] == 0x66 &&
        rom[candidate + 0x108] == 0xcc && rom[candidate + 0x109] == 0x0d &&
        rom[candidate + 0x147] >= 0x0b && rom[candidate + 0x147] <= 0x0d)
      header = candidate;
  }
  if (config.board == Board::Auto) {
    config.board = Board::Linear;
    const auto type = rom[header + 0x147];
    switch (type) {
    case 1:
    case 2:
    case 3:
      config.board = Board::Mbc1;
      break;
    case 5:
    case 6:
      config.board = Board::Mbc2;
      break;
    case 0x0b:
    case 0x0c:
    case 0x0d:
      config.board = Board::Mmm01;
      break;
    case 0x0f:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
      config.board = Board::Mbc3;
      break;
    case 0x19:
    case 0x1a:
    case 0x1b:
    case 0x1c:
    case 0x1d:
    case 0x1e:
      config.board = Board::Mbc5;
      break;
    case 0x20:
      config.board = Board::Mbc6;
      break;
    case 0x22:
      config.board = Board::Mbc7;
      break;
    case 0xfd:
      config.board = Board::Tama;
      break;
    case 0xfe:
      config.board = Board::Huc3;
      break;
    case 0xff:
      config.board = Board::Huc1;
      break;
    }
    const bool has_ram = type == 2 || type == 3 || type == 5 || type == 6 || type == 8 ||
                         type == 9 || type == 0x0c || type == 0x0d || type == 0x10 ||
                         type == 0x12 || type == 0x13 || type == 0x1a || type == 0x1b ||
                         type == 0x1d || type == 0x1e || type == 0x20 || type == 0xfd ||
                         type == 0xff;
    constexpr std::array<unsigned, 6> ram_sizes{0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000};
    const auto ram_type = rom[header + 0x149];
    config.ram_size = has_ram && ram_type < ram_sizes.size() ? ram_sizes[ram_type] : 0;
    if (config.board == Board::Mbc2)
      config.ram_size = 256;
    if (config.board == Board::Mbc6)
      config.ram_size = 32768;
    if (config.board == Board::Tama)
      config.ram_size = 32;
    config.rtc = type == 0x0f || type == 0x10 || type == 0xfd;
    const auto rom_type = rom[header + 0x148];
    if (config.board == Board::Mbc3 && (rom_type == 7 || config.ram_size > 32768))
      config.board = Board::Mbc30;
    if (config.board == Board::Mbc1 && cartridge_identity::multicart(rom))
      config.board = Board::Mbc1Multicart;
    if (config.board == Board::Mbc7 && (rom[header + 0x143] & 0x80)) {
      std::string label;
      for (unsigned byte = 0x134; byte < 0x13f; ++byte) {
        const auto value = rom[header + byte];
        label += value >= 0x20 && value <= 0x7e ? static_cast<char>(value) : ' ';
      }
      const auto begin = label.find_first_not_of(' '), end = label.find_last_not_of(' ');
      if (begin != std::string::npos)
        label = label.substr(begin, end - begin + 1);
      if (label == "CMASTER" &&
          std::equal(rom.begin() + header + 0x13f, rom.begin() + header + 0x143, "KCEJ"))
        config.eeprom_size = 512;
    }
  }
  if (config.ram_size > 0x20000 || !std::has_single_bit(config.ram_size ? config.ram_size : 1u) ||
      config.eeprom_size < 128 || config.eeprom_size > 2048 ||
      !std::has_single_bit(config.eeprom_size))
    return false;
  auto &state = *implementation_;
  state.config = config;
  state.rom.assign(std::bit_ceil(rom.size()), 0);
  std::copy(rom.begin(), rom.end(), state.rom.begin());
  state.rom_size = static_cast<unsigned>(rom.size());
  for (unsigned address = state.rom_size; address < state.rom.size(); ++address) {
    unsigned mapped = address, length = state.rom_size, base = 0;
    while (!std::has_single_bit(length)) {
      mapped &= std::bit_ceil(length) - 1;
      const auto block = std::bit_floor(length);
      if (mapped < block)
        break;
      base += block;
      mapped -= block;
      length -= block;
    }
    state.rom[address] = state.rom[base + (mapped & (std::bit_ceil(length) - 1))];
  }
  state.ram.assign(config.ram_size, 255);
  state.flash.assign(config.board == Board::Mbc6 ? 0x100000 : 0, 255);
  state.eeprom.assign(config.board == Board::Mbc7 ? config.eeprom_size : 0, 255);
  state.board_present = true;
  state.time = state.latched = {};
  state.latch = false;
  state.time_index = 0;
  state.serial_writable = false;
  state.rumble = false;
  state.calendar = {};
  if (config.rtc && config.board == Board::Tama)
    state.calendar[6] = 255;
  state.initialize_clock();
  power();
  return true;
}

void HandheldCartridge::disconnect() {
  auto clock = std::move(implementation_->clock);
  implementation_ = std::make_unique<Implementation>();
  implementation_->clock = std::move(clock);
}

bool HandheldCartridge::present() const {
  return implementation_->board_present;
}

void HandheldCartridge::power() {
  auto &state = *implementation_;
  state.board_present = true;
  state.rom_bank = 1;
  if (state.config.board == Board::Tama)
    state.rom_bank = 0;
  state.ram_bank = state.base_bank = state.mode = 0;
  state.ram_enabled = state.extra_enabled = false;
  state.region_bank = {};
  state.ram_region = {};
  state.region_flash = {};
  state.flash_enabled = state.flash_writable = false;
  state.tilt_x = state.tilt_y = 0x81d0;
  state.select = state.input = state.output = state.index = 0;
  state.ready = false;
  state.serial_reset();
}

std::uint8_t HandheldCartridge::read(std::uint16_t address) {
  if (address > 0x7fff && (address < 0xa000 || address > 0xbfff))
    return 0;
  return implementation_->read(address);
}

void HandheldCartridge::write(std::uint16_t address, std::uint8_t value) {
  if (address <= 0x7fff || (address >= 0xa000 && address <= 0xbfff))
    implementation_->write(address, value);
}

void HandheldCartridge::motion(std::int16_t x, std::int16_t y) {
  implementation_->motion_x = x;
  implementation_->motion_y = y;
}

bool HandheldCartridge::rumbling() const {
  return implementation_->rumble;
}
std::span<std::uint8_t> HandheldCartridge::save_ram() {
  return implementation_->ram;
}
std::span<std::uint8_t> HandheldCartridge::flash() {
  return implementation_->flash;
}
std::span<std::uint8_t> HandheldCartridge::eeprom() {
  return implementation_->eeprom;
}
bool HandheldCartridge::load_clock(std::span<const std::uint8_t> data) {
  return implementation_->load_clock(data);
}
std::vector<std::uint8_t> HandheldCartridge::save_clock() const {
  return implementation_->save_clock();
}

} // namespace cupid::n64
