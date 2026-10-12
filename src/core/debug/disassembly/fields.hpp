#pragma once

#include "core/debug/disassembly.hpp"
#include <bit>
#include <initializer_list>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace cupid::n64::disassembly {

inline std::string hex(std::uint64_t value, unsigned digits = 0) {
  std::ostringstream out;
  out << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(digits) << value;
  return out.str();
}

inline std::string reg(unsigned index, char prefix = 'r') {
  return std::string("$") + prefix + std::to_string(index);
}

inline std::string operation(std::string_view name, std::initializer_list<std::string> args = {}) {
  if (name.empty())
    return {};
  std::string result(name);
  bool first = true;
  for (const auto &arg : args) {
    result += first ? " " : ", ";
    result += arg;
    first = false;
  }
  return result;
}

struct Fields {
  std::uint64_t pc;
  std::uint32_t word;
  bool rsp;
  unsigned op() const {
    return word >> 26;
  }
  unsigned rs() const {
    return (word >> 21) & 31;
  }
  unsigned rt() const {
    return (word >> 16) & 31;
  }
  unsigned rd() const {
    return (word >> 11) & 31;
  }
  unsigned sa() const {
    return (word >> 6) & 31;
  }
  unsigned fn() const {
    return word & 63;
  }
  std::int16_t immediate() const {
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(word));
  }
  std::string target(bool jump = false) const {
    const auto address = jump
                             ? ((pc + 4) & ~0x0fffffffull) | (std::uint64_t(word & 0x03ffffff) << 2)
                             : pc + 4 + static_cast<std::uint64_t>(std::int64_t(immediate()) * 4);
    return hex(rsp ? address & 0xfff : address, rsp ? 3 : 16);
  }
  std::string memory(int offset) const {
    return std::to_string(offset) + "(" + reg(rs()) + ")";
  }
};

std::string scalar(const Fields &f);
std::string cpu(const Fields &f);
std::string signal(const Fields &f);

} // namespace cupid::n64::disassembly
