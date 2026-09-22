#include "cupid/bus.hpp"

#include <algorithm>
#include <limits>

namespace cupid {

void Bus::add_mouse_input(unsigned port, MouseInput input) {
    if (port >= controllers_.size() || !controllers_[port].connected ||
        controllers_[port].device != ControllerDevice::Mouse)
        return;
    auto& pending = mouse_inputs_[port];
    const auto add = [](s32 value, s32 delta) {
        return static_cast<s32>(std::clamp<s64>(static_cast<s64>(value) + delta,
                                                std::numeric_limits<s32>::min(),
                                                std::numeric_limits<s32>::max()));
    };
    pending.left = input.left;
    pending.right = input.right;
    pending.delta_x = add(pending.delta_x, input.delta_x);
    pending.delta_y = add(pending.delta_y, input.delta_y);
}

void Bus::execute_mouse(unsigned port, u8 recv, u8 command, u8* output, bool& valid, bool& overflow) {
    if (command == 0x00 || command == 0xff) {
        output[0] = 0x02;
        output[1] = 0;
        output[2] = 0x02;
        valid = true;
        return;
    }
    if (command != 0x01)
        return;
    auto& pending = mouse_inputs_[port];
    output[0] = static_cast<u8>((pending.left ? 0x80U : 0U) | (pending.right ? 0x40U : 0U));
    output[1] = 0;
    output[2] = static_cast<u8>(std::clamp<s64>(pending.delta_x, -128, 127));
    output[3] = static_cast<u8>(std::clamp<s64>(-static_cast<s64>(pending.delta_y), -128, 127));
    pending.delta_x = 0;
    pending.delta_y = 0;
    valid = true;
    overflow = recv > 4;
}

} // namespace cupid
