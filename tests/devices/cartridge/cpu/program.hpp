#pragma once
#include "../../system/cpu/program.hpp"

namespace test::save_cpu {
using namespace cpu_bus;
constexpr std::array<std::uint8_t, 32> ClockStorage{
    0,    0,    0,    0,    0, 0,    0,    0, 0x31, 0x42, 0x53, 0x64, 0x75, 0x86, 0x97, 0xa8,
    0x59, 0x59, 0xa3, 0x31, 2, 0x12, 0x24, 1, 0,    0,    0,    0,    0xee, 0x6b, 0x28, 0};

} // namespace test::save_cpu
