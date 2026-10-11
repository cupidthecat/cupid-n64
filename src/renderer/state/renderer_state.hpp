#pragma once

#include "core/state/core_state.hpp"
#include "renderer/vulkan/renderer.hpp"

namespace RDP {
class CommandProcessor;
}

namespace cupid::n64 {

class RendererState {
public:
  static void require_memory(HardwareRenderer &renderer, Rdram &memory);
  static void fence(HardwareRenderer &renderer);
  static std::vector<std::uint8_t> capture(HardwareRenderer &renderer);
  static StateCheckpoint checkpoint(HardwareRenderer &renderer);
  static std::unique_ptr<state::Archive> prepare(HardwareRenderer &renderer,
                                                 std::span<const std::uint8_t> data);

private:
  static void registers(state::Archive &archive, RDP::CommandProcessor &processor);
  static void visit(state::Archive &archive, HardwareRenderer &renderer);
};

} // namespace cupid::n64
