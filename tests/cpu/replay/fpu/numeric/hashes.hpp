#pragma once

#include <array>
#include <cstdint>

namespace test::cpu_replay::fpu::numeric {

struct HashUpdate {
  unsigned group;
  std::uint64_t hash;
};

} // namespace test::cpu_replay::fpu::numeric
