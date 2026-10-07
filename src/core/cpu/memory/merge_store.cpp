#include "core/cpu/cpu.hpp"

namespace cupid::n64 {

void Cpu::store_merge(std::uint64_t value, std::uint64_t address, unsigned bytes, bool left) {
  if (bytes == 8 && !require_doubleword())
    return;
  if (bytes == 4)
    value &= 0xffffffff;
  const auto offset = static_cast<unsigned>(address & (bytes - 1));
  const auto base = address & ~std::uint64_t(bytes - 1);
  const bool little = little_endian();
  const auto length =
      left ? (little ? offset + 1 : bytes - offset) : (little ? bytes - offset : offset + 1);
  const auto start = left != little ? offset : 0;
  const bool right_word = !left && !little && bytes == 4;
  unsigned remaining = length;

  if (!little && !right_word) {
    auto position = start;
    while (remaining) {
      auto size = bytes;
      while (size > remaining || (position & (size - 1)))
        size >>= 1;
      const auto shift = (left ? bytes - (position - start) - size : remaining - size) * 8;
      if (!write(base + position, size, value >> shift))
        return;
      position += size;
      remaining -= size;
    }
    return;
  }

  while (remaining) {
    unsigned size = 1;
    if (!left && little) {
      size = bytes;
      while (size > remaining || ((start + remaining - size) & (size - 1)))
        size >>= 1;
    } else {
      while (!(remaining & size))
        size <<= 1;
    }
    const auto position = start + remaining - size;
    const auto transfer_address = right_word && size == length ? address : base + position;
    const auto shift = left     ? (bytes - length + position) * 8
                       : little ? (position - start) * 8
                                : (length - remaining) * 8;
    if (!write(transfer_address, size, value >> shift, !right_word))
      return;
    remaining -= size;
  }
}

} // namespace cupid::n64
