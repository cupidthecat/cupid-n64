#pragma once

#include <array>
#include <cstdint>

namespace test::reset_reuse {

inline constexpr std::array<std::uint64_t, 16> expected{
    0xe1d80663dc2705fbull, 0x771f802b481ce0bfull, 0xad09e6e2bec7f5bbull, 0x935135ee8830353full,
    0xe1d80663dc2705fbull, 0x771f802b481ce0bfull, 0xad09e6e2bec7f5bbull, 0x935135ee8830353full,
    0xe1d80663dc2705fbull, 0x771f802b481ce0bfull, 0xad09e6e2bec7f5bbull, 0x935135ee8830353full,
    0xe1d80663dc2705fbull, 0x771f802b481ce0bfull, 0xad09e6e2bec7f5bbull, 0x935135ee8830353full,
};

} // namespace test::reset_reuse
