#include "implementation.hpp"
#include <algorithm>

namespace cupid::n64 {

std::uint8_t HandheldCartridge::Implementation::memory(const std::vector<std::uint8_t> &data,
                                                       unsigned address) {
  return data.empty() ? 255 : data[address & (data.size() - 1)];
}

void HandheldCartridge::Implementation::store(std::vector<std::uint8_t> &data, unsigned address,
                                              std::uint8_t value) {
  if (!data.empty())
    data[address & (data.size() - 1)] = value;
}

std::uint8_t HandheldCartridge::Implementation::read(std::uint16_t address) {
  if (rom.empty())
    return 255;
  const auto board = config.board;
  if (address <= 0x7fff) {
    if (board == Board::Linear)
      return memory(rom, address);
    if (board == Board::Mmm01 && !mode)
      return memory(rom, rom_size - 0x8000 + address);
    if (address <= 0x3fff) {
      if (board == Board::Mbc1 && mode)
        return memory(rom, (ram_bank << 19) | address);
      if (board == Board::Mbc1Multicart && mode)
        return memory(rom, ((rom_bank >> 4) << 18) | address);
      if (board == Board::Mmm01)
        return memory(rom, (base_bank << 14) + address);
      return memory(rom, address);
    }
    if (board == Board::Mbc6) {
      const auto region = (address >> 13) & 1;
      return memory(region_flash[region] ? flash : rom,
                    (region_bank[region] << 13) | (address & 0x1fff));
    }
    unsigned bank = rom_bank;
    if (board == Board::Mbc1)
      bank |= ram_bank << 5;
    if (board == Board::Mmm01)
      bank += base_bank;
    return memory(rom, (bank << 14) | (address & 0x3fff));
  }
  if (board == Board::Tama)
    return tama_read(address);
  if (board == Board::Mmm01 && !mode)
    return 0;
  if (board == Board::Mbc7) {
    if (address > 0xafff)
      return 0;
    if (!ram_enabled || !extra_enabled)
      return 255;
    switch ((address >> 4) & 15) {
    case 2:
      return static_cast<std::uint8_t>(tilt_x);
    case 3:
      return static_cast<std::uint8_t>(tilt_x >> 8);
    case 4:
      return static_cast<std::uint8_t>(tilt_y);
    case 5:
      return static_cast<std::uint8_t>(tilt_y >> 8);
    case 6:
      return 0;
    case 7:
      return 255;
    case 8:
      return serial_read();
    default:
      return 255;
    }
  }
  if (board == Board::Mbc3 || board == Board::Mbc30) {
    if (!ram_enabled)
      return 255;
    if (ram_bank <= (board == Board::Mbc30 ? 7u : 3u))
      return memory(ram, (ram_bank << 13) | (address & 0x1fff));
    return ram_bank >= 8 && ram_bank <= 12 ? latched[ram_bank - 8] : 255;
  }
  if (board == Board::Huc3 && (!ram_enabled || ram.empty()))
    return 1;
  if (board != Board::Linear && board != Board::Mbc1Multicart && board != Board::Huc1 &&
      !ram_enabled)
    return 255;
  if (ram.empty())
    return board == Board::Linear || board == Board::Mbc1Multicart ? 0 : 255;
  if (board == Board::Mbc2)
    return 0xf0 | ((memory(ram, (address >> 1) & 255) >> ((address & 1) * 4)) & 15);
  if (board == Board::Mbc6)
    return memory(ram, (ram_region[(address >> 12) & 1] << 12) | (address & 0xfff));
  const auto bank =
      board == Board::Linear || board == Board::Mbc1Multicart || (board == Board::Mbc1 && !mode)
          ? 0
          : ram_bank;
  return memory(ram, (bank << 13) | (address & 0x1fff));
}

void HandheldCartridge::Implementation::write(std::uint16_t address, std::uint8_t value) {
  if (rom.empty())
    return;
  const auto board = config.board;
  if (board == Board::Tama)
    return tama_write(address, value);
  if (board == Board::Linear) {
    if (address >= 0xa000)
      store(ram, address, value);
    return;
  }
  if (board == Board::Mmm01 && !mode) {
    if (address < 0x2000)
      mode = 1;
    else if (address < 0x4000)
      base_bank = value & 63;
    return;
  }
  if (board == Board::Mbc1Multicart) {
    if (address >= 0x2000 && address < 0x4000)
      rom_bank = (rom_bank & 0x30) | (value & 15);
    if (address >= 0x4000 && address < 0x6000)
      rom_bank = (rom_bank & 15) | ((value & 3) << 4);
    if (address >= 0x6000 && address < 0x8000)
      mode = value & 1;
    if (address >= 0xa000)
      store(ram, address & 0x3fff, value);
    return;
  }
  if (board == Board::Mbc2) {
    if (address < 0x4000) {
      if (address & 0x100)
        rom_bank = std::max(1u, unsigned(value & 15));
      else
        ram_enabled = (value & 15) == 10;
    } else if (address >= 0xa000 && ram_enabled && !ram.empty()) {
      auto byte = memory(ram, (address >> 1) & 255);
      const auto shift = (address & 1) * 4;
      byte = static_cast<std::uint8_t>((byte & ~(15 << shift)) | ((value & 15) << shift));
      store(ram, (address >> 1) & 255, byte);
    }
    return;
  }
  if (board == Board::Mbc6) {
    if (address < 0x400)
      ram_enabled = (value & 15) == 10;
    else if (address < 0x800)
      ram_region[0] = value & 7;
    else if (address < 0xc00)
      ram_region[1] = value & 7;
    else if (address < 0x1000)
      flash_enabled = value & 1;
    else if (address < 0x2000)
      flash_writable = value & 1;
    else if (address < 0x2800)
      region_bank[0] = value & 127;
    else if (address < 0x3000)
      region_flash[0] = value & 8;
    else if (address < 0x3800)
      region_bank[1] = value & 127;
    else if (address < 0x4000)
      region_flash[1] = value & 8;
    else if (address >= 0xa000 && ram_enabled)
      store(ram, (ram_region[(address >> 12) & 1] << 12) | (address & 0xfff), value);
    return;
  }
  if (address < 0x2000) {
    ram_enabled = (value & 15) == 10;
    if (board == Board::Mbc7 && !ram_enabled)
      extra_enabled = false;
    return;
  }
  if (address < 0x4000) {
    if (board == Board::Mbc5) {
      if (address < 0x3000)
        rom_bank = (rom_bank & 0x100) | value;
      else
        rom_bank = (rom_bank & 255) | ((value & 1) << 8);
    } else {
      unsigned bank = value;
      if (board == Board::Mbc1)
        bank &= 31;
      if (board == Board::Mbc3)
        bank &= 127;
      rom_bank = board == Board::Huc3 || board == Board::Mmm01 ? bank : std::max(1u, bank);
    }
    return;
  }
  if (address < 0x6000) {
    if (board == Board::Mbc7) {
      if (ram_enabled)
        extra_enabled = value == 0x40;
      return;
    }
    ram_bank = value;
    if (board == Board::Mbc1)
      ram_bank &= 3;
    if (board == Board::Mbc3 || board == Board::Mbc30 || board == Board::Mbc5)
      ram_bank &= 15;
    if (board == Board::Mbc5)
      rumble = value & 8;
    return;
  }
  if (address < 0x8000) {
    if (board == Board::Mbc1 || board == Board::Huc1)
      mode = value & 1;
    if (board == Board::Mbc3 || board == Board::Mbc30) {
      if (!latch && value == 1)
        latched = time;
      latch = value & 1;
    }
    return;
  }
  if (board == Board::Mbc7) {
    if (address > 0xafff || !ram_enabled || !extra_enabled)
      return;
    const auto reg = (address >> 4) & 15;
    if (reg == 0 && value == 0x55)
      tilt_x = tilt_y = 0x81d0;
    if (reg == 1 && value == 0xaa) {
      tilt_x = static_cast<std::uint16_t>(std::clamp(0x81d0 - (motion_x >> 8), 0, 65535));
      tilt_y = static_cast<std::uint16_t>(std::clamp(0x81d0 - (motion_y >> 8), 0, 65535));
    }
    if (reg == 8)
      serial_write(value);
    return;
  }
  if (!ram_enabled)
    return;
  if (board == Board::Mbc3 || board == Board::Mbc30) {
    if (ram_bank > (board == Board::Mbc30 ? 7u : 3u)) {
      if (ram_bank >= 8 && ram_bank <= 12) {
        constexpr std::array<std::uint8_t, 5> masks{63, 63, 31, 255, 0xc1};
        time[ram_bank - 8] = value & masks[ram_bank - 8];
      }
      return;
    }
  }
  const auto bank = board == Board::Mbc1 && !mode ? 0 : ram_bank;
  store(ram, (bank << 13) | (address & 0x1fff), value);
}

} // namespace cupid::n64
