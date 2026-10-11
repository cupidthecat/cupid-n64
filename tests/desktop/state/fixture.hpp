#pragma once

#include "desktop/session.hpp"
#include <chrono>
#include <fstream>

namespace session_test {

struct Directory {
  std::filesystem::path path;
  explicit Directory(std::filesystem::path selected = {}) : path(std::move(selected)) {
    if (!path.empty()) {
      std::filesystem::create_directories(path);
      return;
    }
    auto number = std::chrono::steady_clock::now().time_since_epoch().count();
    do {
      path = std::filesystem::temp_directory_path() /
             ("cupid-session-state-" + std::to_string(number++));
    } while (!std::filesystem::create_directory(path));
    temporary = true;
  }
  ~Directory() {
    if (temporary) {
      std::error_code error;
      std::filesystem::remove_all(path, error);
    }
  }
  bool temporary = false;
};

inline void media(const std::filesystem::path &directory, unsigned short_line_clocks = 0) {
  const bool short_video_period = short_line_clocks != 0;
  std::vector<std::uint8_t> cartridge(4096);
  cartridge[0] = 0x80;
  cartridge[1] = 0x37;
  cartridge[2] = 0x12;
  cartridge[3] = 0x40;
  cartridge[0x3e] = 'E';
  cupid::desktop::write_save(directory / "game.z64", cartridge);
  std::vector<std::uint32_t> code;
  const auto store = [&](std::uint32_t address, std::uint32_t value) {
    code.push_back(0x3c010000 | (address >> 16));
    code.push_back(0x34210000 | (address & 0xffff));
    code.push_back(0x3c020000 | (value >> 16));
    code.push_back(0x34420000 | (value & 0xffff));
    code.push_back(0xac220000);
  };
  store(0xa4700008, 0);
  store(0xa470000c, 0x14);
  store(0xa4300000, 0x10f);
  store(0xa3f80008, 0x00080008);
  for (unsigned n = 0; n < 4; ++n) {
    store(0xa3f0000c, 0x02000000);
    store(0xa3f00004, ((4 + n) * 2) << 26);
  }
  for (unsigned n = 0; n < 4; ++n)
    store(0xa3f00004 + ((4 + n) * 2) * 0x400, n * 2 << 26);
  store(0xa0001000, 0xf80007c0);
  store(0xa4400004, 0x1000);
  store(0xa4400008, 1);
  store(0xa4400018, short_video_period ? 3 : 525);
  store(0xa440001c, short_video_period ? short_line_clocks - 1 : 3092);
  store(0xa4400020,
        short_video_period ? ((short_line_clocks << 16) | short_line_clocks) : 0x0c150c15);
  store(0xa4400024, (108u << 16) | 128);
  store(0xa4400028, short_video_period ? 2 : ((34u << 16) | 36));
  store(0xa4400000, 2);
  store(0xa0003000, 0x4000c000);
  store(0xa0003004, 0x2000e000);
  store(0xa4500008, 1);
  store(0xa4500010, 1519);
  store(0xa4500014, 15);
  if (short_video_period) {
    store(0xa0002000, 0x1000ffff);
    store(0xa0002004, 0);
    code.push_back(0x3c017fff);
    code.push_back(0x3421ffff);
    code.push_back(0x40815800);
    code.push_back(0x3c018000);
    code.push_back(0x34212000);
    code.push_back(0x00200008);
    code.push_back(0);
  }
  const auto loop = code.size();
  if (!short_video_period) {
    store(0xa4500000, 0x3000);
    store(0xa4500004, 8);
  }
  const auto distance = static_cast<int>(loop) - static_cast<int>(code.size()) - 1;
  code.push_back(0x10000000 | static_cast<std::uint16_t>(distance));
  code.push_back(0);
  std::vector<std::uint8_t> pif(0x7c0);
  for (unsigned n = 0; n < code.size(); ++n)
    for (unsigned b = 0; b < 4; ++b)
      pif.at(n * 4 + b) = static_cast<std::uint8_t>(code[n] >> (24 - b * 8));
  cupid::desktop::write_save(directory / "pif.bin", pif);
}

inline void advance(cupid::desktop::Session &session, unsigned target) {
  const auto limit = session.clocks() + 2 * cupid::n64::clock_frequency;
  while (session.frames < target && session.clocks() < limit)
    session.run(static_cast<std::uint16_t>((session.frames & 1) ? 0x8000 : 0), 22, -35);
  if (session.frames != target)
    throw std::runtime_error("Session did not reach the requested frame.");
}

} // namespace session_test
