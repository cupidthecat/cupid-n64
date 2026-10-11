#pragma once

#include <array>
#include <cstdint>

namespace cupid::desktop {

struct FeedbackState {
  bool active = false;
  std::uint64_t commands = 0;
};

class FeedbackLatch {
public:
  void reset(const std::array<FeedbackState, 4> &state) {
    for (unsigned port = 0; port < state.size(); ++port) {
      commands_[port] = state[port].commands;
      blocked_[port] = true;
    }
  }
  void clear() {
    blocked_.fill(false);
  }
  std::array<bool, 4> update(const std::array<FeedbackState, 4> &state) {
    std::array<bool, 4> motors{};
    for (unsigned port = 0; port < state.size(); ++port) {
      if (!state[port].active || state[port].commands != commands_[port])
        blocked_[port] = false;
      motors[port] = state[port].active && !blocked_[port];
    }
    return motors;
  }

private:
  std::array<std::uint64_t, 4> commands_{};
  std::array<bool, 4> blocked_{};
};

} // namespace cupid::desktop
