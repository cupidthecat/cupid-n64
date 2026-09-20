#pragma once

#include "cupid/types.hpp"

namespace cupid {

enum class ControllerAccessory {
    None,
    ControllerPak,
    RumblePak,
};

struct ControllerState {
    bool connected{true};
    u16 buttons{};
    s8 stick_x{};
    s8 stick_y{};
    ControllerAccessory accessory{ControllerAccessory::ControllerPak};
};

} // namespace cupid
