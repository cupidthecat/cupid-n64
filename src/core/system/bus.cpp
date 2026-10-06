#include "core/system/console.hpp"

namespace cupid::n64 {

std::span<const std::uint32_t> Console::instruction_data(std::uint32_t address) const {
  if (!ram_.identity() || address >= ram_.size())
    return {};
  return ram_.words().subspan(address >> 2);
}

BusRead Console::read(std::uint32_t address, unsigned bytes) {
  if (address <= 0x03ffffff)
    return mi_.read_rdram(address, bytes);
  if (bytes == 8) {
    frozen_ = true;
    return {};
  }
  if (address <= 0x040bffff)
    return rsp_.read(address, bytes);
  if (address <= 0x040fffff || (address >= 0x04900000 && address <= 0x04ffffff) ||
      address > 0x7fffffff) {
    frozen_ = true;
    return {};
  }
  if (address >= 0x05000000) {
    if (address >= 0x1fc00000 && address <= 0x1fcfffff)
      return si_.read(address, bytes);
    return pi_.read(address, bytes);
  }
  auto value = std::uint64_t(read_register(address));
  if (bytes != 4)
    value >>= (4 - bytes - (address & (4 - bytes))) * 8;
  return {value, 40};
}

BusWrite Console::write(std::uint32_t address, unsigned bytes, std::uint64_t value) {
  if (address <= 0x03ffffff)
    return mi_.write_rdram(address, bytes, value);
  if (address <= 0x040bffff)
    return rsp_.write(address, bytes, value, rsp_.clocks() - pending_clocks());
  if (address <= 0x040fffff || (address >= 0x04900000 && address <= 0x04ffffff) ||
      address > 0x7fffffff) {
    frozen_ = true;
    return {};
  }
  if (address >= 0x05000000) {
    if (address >= 0x1fc00000 && address <= 0x1fcfffff)
      return si_.write(address, bytes, value);
    return pi_.write(address, bytes, value);
  }
  auto word = static_cast<std::uint32_t>(bytes == 8 ? value >> 32 : value);
  if (bytes != 4 && bytes != 8)
    word <<= (4 - bytes - (address & (4 - bytes))) * 8;
  write_register(address, word);
  return {};
}

std::uint32_t Console::read_register(std::uint32_t address) {
  switch (address >> 20) {
  case 0x041:
    return rdp_.read_word(address, pending_clocks() + 40);
  case 0x042:
    return rdp_.read_test(address);
  case 0x043:
    return mi_.read_word(address);
  case 0x044:
    return vi_.read_word(address);
  case 0x045:
    return audio_.read_word(address);
  case 0x046:
    return pi_.read_io(address);
  case 0x047:
    return ri_.read_word(address);
  case 0x048:
    return si_.read_io(address);
  default:
    return 0;
  }
}

void Console::write_register(std::uint32_t address, std::uint32_t value) {
  switch (address >> 20) {
  case 0x041:
    rdp_.write_word(address, value, pending_clocks());
    break;
  case 0x042:
    rdp_.write_test(address, value);
    break;
  case 0x043:
    mi_.write_word(address, value);
    break;
  case 0x044:
    vi_.write_word(address, value);
    break;
  case 0x045:
    audio_.write_word(address, value);
    break;
  case 0x046:
    pi_.write_io(address, value);
    break;
  case 0x047:
    ri_.write_word(address, value);
    break;
  case 0x048:
    si_.write_io(address, value);
    break;
  default:
    break;
  }
}

BusWrite Console::read_burst(std::uint32_t address, std::span<std::uint32_t> words) {
  if (address <= 0x03ffffff)
    return mi_.read_burst(address, words);
  frozen_ = true;
  return {0, false};
}

BusWrite Console::write_burst(std::uint32_t address, std::span<const std::uint32_t> words) {
  if (address <= 0x03ffffff)
    return mi_.write_burst(address, words);
  frozen_ = true;
  return {0, false};
}

} // namespace cupid::n64
