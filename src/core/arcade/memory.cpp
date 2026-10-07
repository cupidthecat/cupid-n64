#include "core/arcade/aleck64.hpp"
#include <algorithm>

namespace cupid::n64 {
namespace {

std::uint32_t read_memory(std::span<const std::uint32_t> memory, std::uint32_t address,
                          unsigned bytes) {
  const auto word = memory[(address >> 2) & (memory.size() - 1)];
  if (bytes == 4)
    return word;
  const auto shift = (4 - bytes - (address & (4 - bytes))) * 8;
  return (word >> shift) & (bytes == 1 ? 0xff : 0xffff);
}

void write_memory(std::span<std::uint32_t> memory, std::uint32_t address, unsigned bytes,
                  std::uint32_t value) {
  if (bytes == 8) {
    write_memory(memory, address, 4, 0);
    write_memory(memory, address + 4, 4, value);
    return;
  }
  auto &word = memory[(address >> 2) & (memory.size() - 1)];
  if (bytes == 4) {
    word = value;
    return;
  }
  const auto shift = (4 - bytes - (address & (4 - bytes))) * 8;
  const auto mask = (bytes == 1 ? 0xffu : 0xffffu) << shift;
  word = (word & ~mask) | ((value << shift) & mask);
}

std::uint32_t format_port(std::uint32_t value, std::uint32_t address, unsigned bytes) {
  return bytes == 4 ? value : value >> ((4 - bytes - (address & (4 - bytes))) * 8);
}

} // namespace

BusRead Aleck64::read(std::uint32_t address, unsigned bytes) const {
  if (bytes == 8)
    return {0, 0, false};
  if (address <= 0xc07fffff)
    return {read_memory(sdram_, address, bytes)};
  if (address <= 0xc0800fff) {
    const auto aligned = address & ~3u;
    if (aligned == 0xc0800000 || aligned == 0xc0800004 || aligned == 0xc0800008)
      return {format_port(read_port((aligned & 15) / 4), address, bytes)};
    if (aligned == 0xc0800100)
      return {0};
  }
  const bool e90 = profile_ == ArcadeProfile::MagicalTetris;
  const auto video = e90 ? 0xd0000000u : 0xd0800000u;
  const auto palette = e90 ? 0xd0010000u : 0xd0801000u;
  const auto registers = e90 ? 0xd0030000u : 0xd0802000u;
  if (address >= video && address <= video + 4095)
    return {read_memory(video_ram_, address, bytes)};
  if (address >= palette && address <= palette + 4095)
    return {read_memory(palette_ram_, address, bytes)};
  if (address >= registers && address <= registers + 31) {
    const auto value = address == registers ? unsigned(!video_enabled_) : 0xffffffffu;
    return {format_port(value, address, bytes)};
  }
  return {0xffffffff};
}

BusWrite Aleck64::write(std::uint32_t address, unsigned bytes, std::uint64_t value) {
  const auto data = static_cast<std::uint32_t>(value);
  if (address <= 0xc07fffff) {
    write_memory(sdram_, address, bytes, data);
    return {};
  }
  if ((address & ~3u) == 0xc0800008) {
    if (profile_ == ArcadeProfile::HiPai || profile_ == ArcadeProfile::SuperRealMahjong)
      mahjong_row_ = static_cast<std::uint8_t>(data >> 8);
    return {};
  }
  const bool e90 = profile_ == ArcadeProfile::MagicalTetris;
  const auto video = e90 ? 0xd0000000u : 0xd0800000u;
  const auto palette = e90 ? 0xd0010000u : 0xd0801000u;
  const auto registers = e90 ? 0xd0030000u : 0xd0802000u;
  if (address >= video && address <= video + 4095)
    write_memory(video_ram_, address, bytes, data);
  if (address >= palette && address <= palette + 4095)
    write_memory(palette_ram_, address, bytes, data);
  if (address == registers + 0x1e)
    video_enabled_ = data & 1;
  return {};
}

BusWrite Aleck64::read_burst(std::uint32_t address, std::span<std::uint32_t> words) const {
  if (address < 0xc0000000 || address > 0xc07fffff)
    return {0, false};
  const auto offset = address & 0xffffff;
  if (offset >= sdram_.size() * 4)
    std::fill(words.begin(), words.end(), 0);
  else
    for (unsigned n = 0; n < words.size(); ++n)
      words[n] = read_memory(sdram_, offset | (n * 4), 4);
  return {};
}

BusWrite Aleck64::write_burst(std::uint32_t address, std::span<const std::uint32_t> words) {
  if (address < 0xc0000000 || address > 0xc07fffff)
    return {0, false};
  const auto offset = address & 0xffffff;
  if (offset < sdram_.size() * 4)
    for (unsigned n = 0; n < words.size(); ++n)
      write_memory(sdram_, offset | (n * 4), 4, words[n]);
  return {};
}

std::span<const std::uint32_t> Aleck64::instruction_data(std::uint32_t address) const {
  if (address < 0xc0000000 || address >= 0xc0400000)
    return {};
  return std::span(sdram_).subspan((address & 0x3fffff) >> 2);
}

} // namespace cupid::n64
