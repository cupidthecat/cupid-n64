#pragma once

#include <array>
#include <bit>
#include <cstdint>

namespace cupid::n64 {

struct RspConstants {
  std::array<std::uint32_t, 32> values{};
  std::uint32_t known = 1;

  bool has(unsigned index) const {
    return (known >> index) & 1;
  }
  std::uint32_t get(unsigned index) const {
    return values[index];
  }
  void set(unsigned index, std::uint32_t value) {
    if (index) {
      known |= 1u << index;
      values[index] = value;
    }
  }
  void track(std::uint32_t instruction, std::uint32_t pc, std::uint32_t written) {
    const auto operation = instruction >> 26;
    const auto source = (instruction >> 21) & 31;
    const auto target = (instruction >> 16) & 31;
    const auto dest = (instruction >> 11) & 31;
    const auto shift = (instruction >> 6) & 31;
    const auto a = get(source);
    const auto b = get(target);
    const bool left = has(source);
    const bool right = has(target);
    const auto immediate = static_cast<std::uint32_t>(
        std::int32_t(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(instruction))));
    known &= ~(written & ~1u);
    if (operation == 0) {
      const auto function = instruction & 63;
      if (function == 9) {
        set(dest, (pc + 8) & 0xfff);
        return;
      }
      if (function == 8 || function == 13)
        return;
      known &= ~(1u << dest) | 1u;
      if (function == 0 || function == 2 || function == 3) {
        if (right)
          set(dest, function == 0   ? b << shift
                    : function == 2 ? b >> shift
                                    : std::uint32_t(std::bit_cast<std::int32_t>(b) >> shift));
        return;
      }
      if (!left || !right)
        return;
      switch (function) {
      case 4:
        set(dest, b << (a & 31));
        break;
      case 6:
        set(dest, b >> (a & 31));
        break;
      case 7:
        set(dest, std::uint32_t(std::bit_cast<std::int32_t>(b) >> (a & 31)));
        break;
      case 32:
      case 33:
        set(dest, a + b);
        break;
      case 34:
      case 35:
        set(dest, a - b);
        break;
      case 36:
        set(dest, a & b);
        break;
      case 37:
        set(dest, a | b);
        break;
      case 38:
        set(dest, a ^ b);
        break;
      case 39:
        set(dest, ~(a | b));
        break;
      case 42:
        set(dest, std::bit_cast<std::int32_t>(a) < std::bit_cast<std::int32_t>(b));
        break;
      case 43:
        set(dest, a < b);
        break;
      }
      return;
    }
    if (operation == 3 || (operation == 1 && (target == 16 || target == 17))) {
      set(31, (pc + 8) & 0xfff);
      return;
    }
    if (operation == 15) {
      set(target, instruction << 16);
      return;
    }
    if (!left)
      return;
    switch (operation) {
    case 8:
    case 9:
      set(target, a + immediate);
      break;
    case 10:
      set(target, std::bit_cast<std::int32_t>(a) < std::bit_cast<std::int32_t>(immediate));
      break;
    case 11:
      set(target, a < immediate);
      break;
    case 12:
      set(target, a & (instruction & 0xffff));
      break;
    case 13:
      set(target, a | (instruction & 0xffff));
      break;
    case 14:
      set(target, a ^ (instruction & 0xffff));
      break;
    }
  }
};

} // namespace cupid::n64
