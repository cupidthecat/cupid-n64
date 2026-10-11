#pragma once

#include "core/state/core_state.hpp"
#include "input.hpp"
#include <functional>
#include <optional>

namespace test::machine_replay {

struct Difference {
  std::size_t boundary;
  cupid::n64::StateDifference state;
};

using Observer = std::function<void(cupid::n64::Console &, Mode, std::size_t)>;

// Both modes restore the same owned starting snapshot. Each action ends at a checkpoint.
std::optional<Difference> compare(const Input &input, const Observer &observe = {});
Input reduce(Input input, const std::function<bool(const Input &)> &fails, unsigned attempts = 128);
void print(const Difference &difference);

} // namespace test::machine_replay
