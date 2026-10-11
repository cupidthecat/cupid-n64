#pragma once

#include "core/state/core_state.hpp"
#include "renderer/vulkan/renderer.hpp"

namespace cupid::n64 {

class MachineState {
public:
  static std::vector<std::uint8_t> capture(Console &console, HardwareRenderer &renderer);
  static StateCheckpoint checkpoint(Console &console, HardwareRenderer &renderer);
  static void restore(Console &console, HardwareRenderer &renderer,
                      std::span<const std::uint8_t> data);
};

} // namespace cupid::n64
