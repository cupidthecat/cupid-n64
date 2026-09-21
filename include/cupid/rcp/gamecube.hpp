#pragma once

#include "cupid/types.hpp"

#include <span>

namespace cupid {

struct GameCubeState {
    u8 buttons0{};
    u8 buttons1{};
    u8 stick_x{127};
    u8 stick_y{127};
    u8 cstick_x{127};
    u8 cstick_y{127};
    u8 trigger_l{};
    u8 trigger_r{};
};

class GameCubeController {
  public:
    void reset();
    void set_state(GameCubeState state);
    void execute(std::span<const u8> input, std::span<u8> output, bool& valid, bool& overflow, bool& rumble);

  private:
    GameCubeState state_{};
    bool origin_pending_{true};

    void write_buttons(std::span<u8> output) const;
    void write_mode(u8 mode, std::span<u8> output) const;
    static void write_origin(std::span<u8> output);
};

} // namespace cupid
