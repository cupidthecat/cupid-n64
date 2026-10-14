#pragma once

#include <array>
#include <cstdint>

namespace test::tracing_context {

struct Case {
  unsigned modes;
  std::uint32_t pc;
  std::array<unsigned, 4> ticks, records;
};

inline constexpr std::array<Case, 12> cases{{
    {0, 0x000, {12, 12, 12, 12}, {0, 0, 0, 0}},
    {0, 0x040, {12, 12, 12, 12}, {0, 0, 0, 0}},
    {0, 0xff8, {12, 12, 12, 12}, {0, 0, 0, 0}},
    {1, 0x000, {12, 15, 12, 15}, {3, 3, 6, 6}},
    {1, 0x040, {12, 15, 12, 15}, {3, 3, 6, 6}},
    {1, 0xff8, {12, 15, 12, 15}, {3, 3, 6, 6}},
    {2, 0x000, {12, 15, 12, 15}, {0, 3, 3, 6}},
    {2, 0x040, {12, 15, 12, 15}, {0, 3, 3, 6}},
    {2, 0xff8, {12, 15, 12, 15}, {0, 3, 3, 6}},
    {3, 0x000, {12, 12, 12, 12}, {3, 6, 9, 12}},
    {3, 0x040, {12, 12, 12, 12}, {3, 6, 9, 12}},
    {3, 0xff8, {12, 12, 12, 12}, {3, 6, 9, 12}},
}};

} // namespace test::tracing_context
