#include "cupid/rcp/gamecube.hpp"

#include <algorithm>
#include <array>

namespace cupid {

void GameCubeController::reset() {
    origin_pending_ = true;
}

void GameCubeController::set_state(GameCubeState state) {
    state.buttons0 &= 0x1fU;
    state.buttons1 &= 0x7fU;
    state_ = state;
}

void GameCubeController::write_buttons(std::span<u8> output) const {
    output[0] = static_cast<u8>(state_.buttons0 | (origin_pending_ ? 0x20U : 0U));
    output[1] = static_cast<u8>(state_.buttons1 | 0x80U);
}

void GameCubeController::write_mode(u8 mode, std::span<u8> output) const {
    switch (mode) {
    case 1:
        output[4] = static_cast<u8>((state_.cstick_x & 0xf0U) | (state_.cstick_y >> 4U));
        output[5] = state_.trigger_l;
        output[6] = state_.trigger_r;
        output[7] = 0;
        return;
    case 2:
        output[4] = static_cast<u8>((state_.cstick_x & 0xf0U) | (state_.cstick_y >> 4U));
        output[5] = static_cast<u8>((state_.trigger_l & 0xf0U) | (state_.trigger_r >> 4U));
        output[6] = 0;
        output[7] = 0;
        return;
    case 3:
        output[4] = state_.cstick_x;
        output[5] = state_.cstick_y;
        output[6] = state_.trigger_l;
        output[7] = state_.trigger_r;
        return;
    case 4:
        output[4] = state_.cstick_x;
        output[5] = state_.cstick_y;
        output[6] = 0;
        output[7] = 0;
        return;
    default:
        output[4] = state_.cstick_x;
        output[5] = state_.cstick_y;
        output[6] = static_cast<u8>((state_.trigger_l & 0xf0U) | (state_.trigger_r >> 4U));
        output[7] = 0;
        return;
    }
}

void GameCubeController::write_origin(std::span<u8> output) {
    output[0] = 0;
    output[1] = 0x80;
    output[2] = 127;
    output[3] = 127;
    output[4] = 127;
    output[5] = 127;
    output[6] = 0;
    output[7] = 0;
    output[8] = 0;
    output[9] = 0;
}

void GameCubeController::execute(std::span<const u8> input, std::span<u8> output, bool& valid, bool& overflow,
                                 bool& rumble) {
    valid = false;
    overflow = false;
    if (input.empty())
        return;
    std::array<u8, 10> reply{};
    std::size_t reply_length = 0;
    const u8 command = input[0];
    switch (command) {
    case 0x00:
    case 0xff:
        reply[0] = 0x09;
        reply[2] = rumble ? 0x08 : 0;
        reply_length = 3;
        break;
    case 0x40:
        if (input.size() < 3)
            return;
        rumble = (input[2] & 1U) != 0;
        write_buttons(reply);
        reply[2] = state_.stick_x;
        reply[3] = state_.stick_y;
        write_mode(input[1], reply);
        reply_length = 8;
        overflow = output.size() > reply_length;
        break;
    case 0x41:
    case 0x42:
        write_origin(reply);
        origin_pending_ = false;
        reply_length = 10;
        overflow = output.size() > reply_length;
        break;
    case 0x43:
        write_buttons(reply);
        reply[2] = state_.stick_x;
        reply[3] = state_.stick_y;
        reply[4] = state_.cstick_x;
        reply[5] = state_.cstick_y;
        reply[6] = state_.trigger_l;
        reply[7] = state_.trigger_r;
        reply_length = 10;
        overflow = output.size() > reply_length;
        break;
    default:
        return;
    }
    std::fill(output.begin(), output.end(), u8{0});
    std::copy_n(reply.begin(), std::min(output.size(), reply_length), output.begin());
    valid = true;
}

} // namespace cupid
