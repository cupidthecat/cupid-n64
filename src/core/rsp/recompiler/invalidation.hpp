#pragma once

#include <bit>
#include <cstdint>

namespace cupid::n64::rsp_cache {

constexpr std::uint64_t line_mask(std::uint32_t address, unsigned bytes) {
  const auto first = (address & 0xfff) >> 6;
  const auto last = ((address + bytes - 1) & 0xfff) >> 6;
  const auto count = ((last + 64 - first) & 63) + 1;
  const auto mask = count == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << count) - 1;
  return std::rotl(mask, static_cast<int>(first));
}

} // namespace cupid::n64::rsp_cache
