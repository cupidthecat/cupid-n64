#include "core/rsp/rsp.hpp"
#include <algorithm>

namespace cupid::n64 {

void Rsp::vector_memory(std::uint32_t instruction) {
  const bool store = (instruction >> 26) == 58;
  const auto source = (instruction >> 21) & 31;
  const auto target = (instruction >> 16) & 31;
  const auto operation = (instruction >> 11) & 31;
  const auto element = (instruction >> 7) & 15;
  const auto immediate = static_cast<std::int32_t>(instruction << 25) >> 25;
  if (operation > 11 || (!store && operation == 10))
    return;
  constexpr unsigned scales[12] = {1, 2, 4, 8, 16, 16, 8, 8, 16, 16, 16, 16};
  auto address = state_.gpr[source] + static_cast<std::uint32_t>(immediate) * scales[operation];
  auto &vector = state_.vectors[target];
  const auto read = [&](unsigned offset) { return memory_[offset & 0xfff]; };
  const auto write = [&](unsigned offset, unsigned value) {
    memory_[offset & 0xfff] = static_cast<std::uint8_t>(value);
  };
  if (operation <= 4) {
    auto length = operation == 4 ? 16 - (address & 15) : scales[operation];
    if (!store)
      length = std::min(length, 16 - element);
    for (unsigned n = 0; n < length; ++n) {
      if (store)
        write(address++, vector.byte((element + n) & 15));
      else
        vector.byte(element + n, read(address++));
    }
  } else if (operation == 5) {
    const auto length = address & 15;
    address &= ~15u;
    if (store) {
      for (unsigned n = 0; n < length; ++n)
        write(address++, vector.byte((element + 16 - length + n) & 15));
    } else {
      const auto start = 16 - (length - element);
      for (unsigned n = start; n < 16; ++n)
        vector.byte(n & 15, read(address++));
    }
  } else if (operation == 6 || operation == 7) {
    if (store) {
      for (unsigned n = element; n < element + 8; ++n) {
        const bool high = ((n & 15) < 8) == (operation == 6);
        write(address++, high ? vector.byte((n & 7) * 2) : vector.lanes[n & 7] >> 7);
      }
    } else {
      const auto index = (address & 7) - element;
      address &= ~7u;
      for (unsigned n = 0; n < 8; ++n)
        vector.lanes[n] = static_cast<std::uint16_t>(read(address + ((index + n) & 15))
                                                     << (operation == 6 ? 8 : 7));
    }
  } else if (operation == 8) {
    const auto index = address & 7;
    address &= ~7u;
    for (unsigned n = 0; n < 8; ++n) {
      if (store) {
        const auto byte = element + n * 2;
        const auto value =
            (unsigned(vector.byte(byte & 15)) << 1) | (vector.byte((byte + 1) & 15) >> 7);
        write(address + ((index + n * 2) & 15), value);
      } else
        vector.lanes[n] =
            static_cast<std::uint16_t>(read(address + ((index - element + n * 2) & 15)) << 7);
    }
  } else if (operation == 9) {
    const auto index = address & 7;
    address &= ~7u;
    if (store) {
      constexpr int first[16] = {0, 6, -1, -1, 1, 7, -1, -1, 4, -1, -1, 3, 5, -1, -1, 0};
      for (unsigned n = 0; n < 4; ++n) {
        const auto first_lane = static_cast<unsigned>(first[element]);
        const auto lane = (first_lane & 4) | ((first_lane + n) & 3);
        write(address + ((index + n * 4) & 15), first[element] < 0 ? 0 : vector.lanes[lane] >> 7);
      }
    } else {
      RspVector temporary;
      for (unsigned n = 0; n < 4; ++n) {
        temporary.lanes[n] =
            static_cast<std::uint16_t>(read(address + ((index - element + n * 4) & 15)) << 7);
        temporary.lanes[n + 4] =
            static_cast<std::uint16_t>(read(address + ((index - element + n * 4 + 8) & 15)) << 7);
      }
      for (unsigned n = element; n < std::min(element + 8, 16u); ++n)
        vector.byte(n, temporary.byte(n));
    }
  } else if (operation == 10) {
    auto index = address & 7;
    address &= ~7u;
    for (unsigned n = 0; n < 16; ++n)
      write(address + ((index++) & 15), vector.byte((element + n) & 15));
  } else if (operation == 11) {
    const auto base = target & ~7u;
    if (store) {
      auto byte = 16 - (element & ~1u);
      auto index = (address & 7) - (element & ~1u);
      address &= ~7u;
      for (unsigned n = 0; n < 8; ++n) {
        write(address + ((index++) & 15), state_.vectors[base + n].byte((byte++) & 15));
        write(address + ((index++) & 15), state_.vectors[base + n].byte((byte++) & 15));
      }
    } else {
      const auto begin = address & ~7u;
      address = begin + ((element + (address & 8)) & 15);
      auto reg = element >> 1;
      for (unsigned n = 0; n < 8; ++n) {
        state_.vectors[base + reg].byte(n * 2, read(address++));
        if (address == begin + 16)
          address = begin;
        state_.vectors[base + reg].byte(n * 2 + 1, read(address++));
        if (address == begin + 16)
          address = begin;
        reg = (reg + 1) & 7;
      }
    }
  }
}

} // namespace cupid::n64
