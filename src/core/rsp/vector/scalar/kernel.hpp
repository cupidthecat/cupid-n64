#pragma once

#include "core/rsp/vector/execute.hpp"
#include <algorithm>
#include <bit>

namespace cupid::n64::scalar {

inline unsigned select_lane(unsigned element, unsigned lane) {
  if (element < 2)
    return lane;
  if (element < 4)
    return (lane & 6) | (element & 1);
  if (element < 8)
    return (lane & 4) | (element & 3);
  return element & 7;
}

inline std::uint16_t clamp(std::int64_t value) {
  return static_cast<std::uint16_t>(std::clamp(value, std::int64_t(-32768), std::int64_t(32767)));
}

inline std::int64_t signed_accumulator(std::uint64_t value) {
  return std::bit_cast<std::int64_t>(value << 16) >> 16;
}

inline void flag(std::uint8_t &flags, unsigned lane, bool value) {
  flags = static_cast<std::uint8_t>((flags & ~(1u << lane)) | (unsigned(value) << lane));
}

inline std::uint16_t saturate_accumulator(std::uint64_t value, bool middle, std::uint16_t negative,
                                          std::uint16_t positive) {
  const auto high = static_cast<std::uint16_t>(value >> 32);
  const auto mid = static_cast<std::uint16_t>(value >> 16);
  if (static_cast<std::int16_t>(high) < 0) {
    if (high != 0xffff || static_cast<std::int16_t>(mid) >= 0)
      return negative;
  } else if (high != 0 || static_cast<std::int16_t>(mid) < 0)
    return positive;
  return static_cast<std::uint16_t>(value >> (middle ? 16 : 0));
}

template <int Operation = -1, int Element = -1>
void execute(RspState &state, RspVector &result, const RspVector &source_vector,
             const RspVector &target_vector, unsigned source, unsigned dynamic_operation = 0,
             unsigned dynamic_element = 0) {
  const auto operation = Operation < 0 ? dynamic_operation : unsigned(Operation);
  const auto element = Element < 0 ? dynamic_element : unsigned(Element);
  if (operation == 0x37 || operation == 0x3f)
    return;
  const auto a = source_vector;
  RspVector b;
  for (unsigned n = 0; n < 8; ++n)
    b.lanes[n] = target_vector.lanes[select_lane(element, n)];
  for (unsigned n = 0; n < 8; ++n) {
    const auto au = a.lanes[n];
    const auto bu = b.lanes[n];
    const auto as = static_cast<std::int16_t>(au);
    const auto bs = static_cast<std::int16_t>(bu);
    const auto carry = (state.carry_low >> n) & 1;
    const bool carry_high = (state.carry_high >> n) & 1;
    const bool compare = (state.compare_low >> n) & 1;
    const bool compare_high = (state.compare_high >> n) & 1;
    const bool extension = (state.extension >> n) & 1;
    auto acc = state.accumulator.get(n);
    const auto low = [&](std::uint16_t value) {
      acc = (acc & ~0xffffull) | value;
      result.lanes[n] = value;
    };
    const auto full = [&](std::int64_t value) {
      acc = static_cast<std::uint64_t>(value) & 0xffffffffffffull;
    };
    const auto add = [&](std::int64_t value) {
      acc = (acc + static_cast<std::uint64_t>(value)) & 0xffffffffffffull;
    };
    switch (operation) {
    case 0x00:
    case 0x01:
    case 0x08:
    case 0x09: {
      const auto product = std::int64_t(as) * bs * 2;
      if (operation < 8)
        full(product + 0x8000);
      else
        add(product);
      const auto high = static_cast<std::int16_t>(acc >> 32);
      const auto middle = static_cast<std::int16_t>(acc >> 16);
      if ((operation & 1) == 0)
        result.lanes[n] = saturate_accumulator(acc, true, 0x8000, 0x7fff);
      else if (high < 0)
        result.lanes[n] = 0;
      else if (operation == 1 ? ((high ^ middle) < 0) : (high != 0 || middle < 0))
        result.lanes[n] = 0xffff;
      else
        result.lanes[n] = static_cast<std::uint16_t>(acc >> 16);
      break;
    }
    case 0x02:
    case 0x0a: {
      const auto product = std::int64_t(bs) * ((source & 1) ? 65536 : 1);
      const auto signed_acc = signed_accumulator(acc);
      if ((operation == 2 && signed_acc >= 0) || (operation == 10 && signed_acc < 0))
        add(product);
      result.lanes[n] = clamp(signed_accumulator(acc) >> 16);
      break;
    }
    case 0x03: {
      auto product = std::int64_t(as) * bs;
      if (product < 0)
        product += 31;
      full(product * 65536);
      result.lanes[n] = clamp(product >> 1) & 0xfff0;
      break;
    }
    case 0x04:
      full((std::uint32_t(au) * bu) >> 16);
      result.lanes[n] = static_cast<std::uint16_t>(acc);
      break;
    case 0x05:
      full(std::int64_t(as) * bu);
      result.lanes[n] = static_cast<std::uint16_t>(acc >> 16);
      break;
    case 0x06:
      full(std::int64_t(au) * bs);
      result.lanes[n] = static_cast<std::uint16_t>(acc);
      break;
    case 0x07:
      full(std::int64_t(as) * bs * 65536);
      result.lanes[n] = saturate_accumulator(acc, true, 0x8000, 0x7fff);
      break;
    case 0x0b: {
      auto product =
          std::int64_t(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(acc >> 16)));
      if (product < 0 && !(product & 32))
        product += 32;
      else if (product >= 32 && !(product & 32))
        product -= 32;
      acc = (static_cast<std::uint64_t>(product) * 65536 & 0xffffffffffffull) | (acc & 0xffff);
      result.lanes[n] = clamp(product >> 1) & 0xfff0;
      break;
    }
    case 0x0c:
      add((std::uint32_t(au) * bu) >> 16);
      result.lanes[n] = saturate_accumulator(acc, false, 0, 0xffff);
      break;
    case 0x0d:
      add(std::int64_t(as) * bu);
      result.lanes[n] = saturate_accumulator(acc, true, 0x8000, 0x7fff);
      break;
    case 0x0e:
      add(std::int64_t(au) * bs);
      result.lanes[n] = saturate_accumulator(acc, false, 0, 0xffff);
      break;
    case 0x0f:
      add(std::int64_t(as) * bs * 65536);
      result.lanes[n] = saturate_accumulator(acc, true, 0x8000, 0x7fff);
      break;
    case 0x10:
    case 0x11: {
      const auto value = operation == 0x10 ? std::int32_t(as) + bs + std::int32_t(carry)
                                           : std::int32_t(as) - bs - std::int32_t(carry);
      low(static_cast<std::uint16_t>(value));
      result.lanes[n] = clamp(value);
      break;
    }
    case 0x13:
      low(static_cast<std::uint16_t>(as < 0 ? -std::int32_t(bs) : as > 0 ? bs : 0));
      if (as < 0 && bs == -32768)
        result.lanes[n] = 0x7fff;
      break;
    case 0x14: {
      const auto value = std::uint32_t(au) + bu;
      low(static_cast<std::uint16_t>(value));
      flag(state.carry_low, n, value >> 16);
      break;
    }
    case 0x15: {
      const auto value = std::uint32_t(au) - bu;
      low(static_cast<std::uint16_t>(value));
      flag(state.carry_low, n, value >> 16);
      flag(state.carry_high, n, value != 0);
      break;
    }
    case 0x1d:
      result.lanes[n] = element >= 8 && element <= 10
                            ? static_cast<std::uint16_t>(acc >> (((10 - element) & 3) * 16))
                            : 0;
      break;
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23: {
      bool condition;
      if (operation == 0x20)
        condition = as < bs || (as == bs && carry && carry_high);
      else if (operation == 0x21)
        condition = !carry_high && au == bu;
      else if (operation == 0x22)
        condition = au != bu || carry_high;
      else
        condition = as > bs || (as == bs && (!carry || !carry_high));
      flag(state.compare_low, n, condition);
      low(condition ? au : bu);
      break;
    }
    case 0x24:
      if (carry) {
        bool condition = compare;
        if (!carry_high) {
          const auto sum = static_cast<std::uint16_t>(au + bu);
          const bool overflow = std::uint32_t(au) + bu != sum;
          condition = extension ? (sum == 0 || !overflow) : (sum == 0 && !overflow);
          flag(state.compare_low, n, condition);
        }
        low(condition ? static_cast<std::uint16_t>(-std::int32_t(bu)) : au);
      } else {
        bool condition = compare_high;
        if (!carry_high) {
          condition = au >= bu;
          flag(state.compare_high, n, condition);
        }
        low(condition ? bu : au);
      }
      break;
    case 0x25:
      if ((as ^ bs) < 0) {
        const auto value = static_cast<std::int16_t>(as + bs);
        low(value <= 0 ? static_cast<std::uint16_t>(-std::int32_t(bs)) : au);
        flag(state.compare_low, n, value <= 0);
        flag(state.compare_high, n, bs < 0);
        flag(state.carry_low, n, true);
        flag(state.carry_high, n, value != 0 && au != (bu ^ 0xffff));
        flag(state.extension, n, value == -1);
      } else {
        const auto value = static_cast<std::int16_t>(as - bs);
        low(value >= 0 ? bu : au);
        flag(state.compare_low, n, bs < 0);
        flag(state.compare_high, n, value >= 0);
        flag(state.carry_low, n, false);
        flag(state.carry_high, n, value != 0 && au != (bu ^ 0xffff));
        flag(state.extension, n, false);
      }
      break;
    case 0x26:
      if ((as ^ bs) < 0) {
        const bool condition = std::int32_t(as) + bs + 1 <= 0;
        flag(state.compare_high, n, bs < 0);
        flag(state.compare_low, n, condition);
        low(condition ? static_cast<std::uint16_t>(~bu) : au);
      } else {
        const bool condition = as >= bs;
        flag(state.compare_low, n, bs < 0);
        flag(state.compare_high, n, condition);
        low(condition ? bu : au);
      }
      break;
    case 0x27:
      low(compare ? au : bu);
      break;
    case 0x28:
      low(au & bu);
      break;
    case 0x29:
      low(static_cast<std::uint16_t>(~(au & bu)));
      break;
    case 0x2a:
      low(au | bu);
      break;
    case 0x2b:
      low(static_cast<std::uint16_t>(~(au | bu)));
      break;
    case 0x2c:
      low(au ^ bu);
      break;
    case 0x2d:
      low(static_cast<std::uint16_t>(~(au ^ bu)));
      break;
    default:
      low(static_cast<std::uint16_t>(as + bs));
      result.lanes[n] = 0;
      break;
    }
    state.accumulator.set(n, acc);
  }
  if (operation == 0x10 || operation == 0x11 || (operation >= 0x20 && operation <= 0x24) ||
      operation == 0x26 || operation == 0x27)
    state.carry_low = state.carry_high = 0;
  if (operation == 0x14)
    state.carry_high = 0;
  if (operation >= 0x20 && operation <= 0x23)
    state.compare_high = 0;
  if (operation == 0x24 || operation == 0x26)
    state.extension = 0;
}

} // namespace cupid::n64::scalar
