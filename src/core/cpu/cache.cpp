#include "core/cpu/cpu.hpp"

namespace cupid::n64 {

bool Cpu::fill(CacheLine &line, std::uint32_t physical, std::uint32_t index, bool instruction) {
  advance_clocks(instruction ? 96 : 80);
  line.tag = physical & ~0xfffu;
  line.dirty = false;
  const auto transfer =
      bus_.read_burst(line.tag | index, std::span(line.words).first(instruction ? 8 : 4));
  advance_clocks(transfer.clocks);
  line.valid = transfer.success;
  if (!transfer.success)
    raise(instruction ? Exception::BusInstruction : Exception::BusData);
  return transfer.success;
}

bool Cpu::writeback(CacheLine &line, std::uint32_t index, bool instruction) {
  advance_clocks(instruction ? 96 : 80);
  const auto transfer =
      bus_.write_burst(line.tag | index, std::span(line.words).first(instruction ? 8 : 4));
  advance_clocks(transfer.clocks);
  if (!transfer.success)
    raise(Exception::BusData);
  return transfer.success;
}

std::optional<std::uint64_t> Cpu::cache_read(std::uint64_t virtual_address, std::uint32_t physical,
                                             unsigned bytes, bool instruction) {
  const unsigned shift = instruction ? 5 : 4;
  const auto index = static_cast<unsigned>((virtual_address >> shift) & 511);
  auto &line = instruction ? icache_[index] : dcache_[index];
  const auto bus_index = (index << shift) & 0xfff;
  if (!line.hit(physical)) {
    if (!instruction && line.valid && line.dirty && !writeback(line, bus_index, false))
      return {};
    if (!fill(line, physical, bus_index, instruction))
      return {};
  } else if (!instruction)
    advance_clocks(2);
  if (instruction)
    return line.words[(physical >> 2) & 7];
  if (bytes == 8) {
    const auto word = (physical >> 2) & 2;
    return (std::uint64_t(line.words[word]) << 32) | line.words[word + 1];
  }
  const auto word = line.words[(physical >> 2) & 3];
  const auto bit = (4 - bytes - (physical & (4 - bytes))) * 8;
  return (std::uint64_t(word) >> bit) & ((1ull << (bytes * 8)) - 1);
}

bool Cpu::cache_write(std::uint64_t virtual_address, std::uint32_t physical, unsigned bytes,
                      std::uint64_t value) {
  const auto index = static_cast<unsigned>((virtual_address >> 4) & 511);
  auto &line = dcache_[index];
  const auto bus_index = (index << 4) & 0xfff;
  if (!line.hit(physical)) {
    if (line.valid && line.dirty && !writeback(line, bus_index, false))
      return false;
    if (!fill(line, physical, bus_index, false))
      return false;
  } else
    advance_clocks(2);
  if (bytes == 8) {
    const auto word = (physical >> 2) & 2;
    line.words[word] = static_cast<std::uint32_t>(value >> 32);
    line.words[word + 1] = static_cast<std::uint32_t>(value);
  } else {
    auto &word = line.words[(physical >> 2) & 3];
    const auto bit = (4 - bytes - (physical & (4 - bytes))) * 8;
    const auto mask = ((1ull << (bytes * 8)) - 1) << bit;
    word = static_cast<std::uint32_t>((word & ~mask) | ((value << bit) & mask));
  }
  line.dirty = true;
  return true;
}

void Cpu::cache_operation(unsigned operation, std::uint64_t address) {
  const auto access = translate(address, 4, false);
  if (!access)
    return;
  const bool instruction = !(operation & 1);
  const auto shift = instruction ? 5u : 4u;
  const auto index = static_cast<unsigned>((address >> shift) & 511);
  auto &line = instruction ? icache_[index] : dcache_[index];
  const auto bus_index = (index << shift) & 0xfff;
  const auto tag = access->physical & ~0xfffu;
  switch (operation) {
  case 0x00:
    line.tag = tag;
    line.valid = false;
    break;
  case 0x01:
    if (line.valid && line.dirty && !writeback(line, bus_index, false))
      return;
    if (line.valid)
      line.tag = tag;
    line.valid = false;
    break;
  case 0x04:
  case 0x05:
    control_[TagLo] = ((line.tag >> 12) << 8) | (line.valid ? (instruction ? 2ull : 3ull) << 6 : 0);
    break;
  case 0x08:
  case 0x09:
    line.tag = static_cast<std::uint32_t>((control_[TagLo] & 0x0fffff00) << 4);
    line.valid = control_[TagLo] & 0x80;
    break;
  case 0x0d:
    if (!line.hit(access->physical)) {
      if (line.valid && line.dirty && !writeback(line, bus_index, false))
        return;
      line.dirty = false;
    }
    line.tag = tag;
    line.valid = true;
    break;
  case 0x10:
  case 0x11:
    if (line.hit(access->physical))
      line.valid = false;
    break;
  case 0x14:
    fill(line, access->physical, bus_index, true);
    break;
  case 0x15:
    if (line.hit(access->physical)) {
      if (line.dirty && !writeback(line, bus_index, false))
        return;
      line.valid = false;
    }
    break;
  case 0x18:
  case 0x19:
    if (line.hit(access->physical) && (instruction || line.dirty)) {
      if (!writeback(line, bus_index, instruction))
        return;
      if (!instruction)
        line.dirty = false;
    }
    break;
  default:
    break;
  }
}

} // namespace cupid::n64
