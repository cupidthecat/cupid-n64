#pragma once

#include "cupid/types.hpp"

namespace cupid {

enum class ControllerDevice {
    Gamepad,
    Mouse,
};

enum class ControllerAccessory {
    None,
    ControllerPak,
    RumblePak,
    BioSensor,
    TransferPak,
};

struct ControllerState {
    bool connected{true};
    u16 buttons{};
    s8 stick_x{};
    s8 stick_y{};
    ControllerAccessory accessory{ControllerAccessory::ControllerPak};
    ControllerDevice device{ControllerDevice::Gamepad};
};

struct MouseInput {
    bool left{};
    bool right{};
    s32 delta_x{};
    s32 delta_y{};
};

} // namespace cupid
