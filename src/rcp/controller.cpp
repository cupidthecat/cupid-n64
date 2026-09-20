#include "cupid/bus.hpp"

#include <algorithm>

namespace cupid {

void Bus::set_controller_state(unsigned port, ControllerState state) {
    if (port >= controllers_.size())
        return;
    const auto& previous = controllers_[port];
    const bool device_changed = state.device != previous.device;
    if (device_changed || state.accessory != previous.accessory ||
        (!previous.connected && state.connected && state.accessory != ControllerAccessory::None))
        controller_pak_changed_[port] = true;
    if (!state.connected || device_changed || state.accessory != previous.accessory)
        controller_rumble_[port] = false;
    if (!state.connected || device_changed)
        mouse_inputs_[port] = {};
    controllers_[port] = state;
}

u8 Bus::address_crc(u16 address) {
    u8 crc = 0;
    for (unsigned index = 0; index < 16; ++index) {
        const u8 feedback = (crc & 0x10U) != 0 ? 0x15U : 0U;
        crc = static_cast<u8>((static_cast<unsigned>(crc) << 1U) | ((address & 0x8000U) != 0 ? 1U : 0U));
        address = static_cast<u16>(address << 1U);
        crc ^= feedback;
    }
    return static_cast<u8>(crc & 0x1fU);
}

u8 Bus::pak_crc(const u8* data) {
    u8 crc = 0;
    for (unsigned byte = 0; byte < 33; ++byte) {
        for (int bit = 7; bit >= 0; --bit) {
            const u8 feedback = (crc & 0x80U) != 0 ? 0x85U : 0U;
            crc = static_cast<u8>(crc << 1U);
            if (byte < 32 && (data[byte] & (1U << static_cast<unsigned>(bit))) != 0)
                crc |= 1U;
            crc ^= feedback;
        }
    }
    return crc;
}

void Bus::execute_controller(unsigned port, u8 send, u8 recv, const u8* input, u8* output, bool& valid,
                             bool& overflow) {
    const u8 command = input[0];
    const ControllerState& controller = controllers_[port];
    if (!controller.connected)
        return;
    if (controller.device == ControllerDevice::Mouse) {
        execute_mouse(port, recv, command, output, valid, overflow);
        return;
    }
    if (command == 0x00 || command == 0xff) {
        output[0] = 0x05;
        output[1] = 0x00;
        output[2] = (controller.accessory != ControllerAccessory::None)
                        ? controller_pak_changed_[port] ? 0x03 : 0x01
                        : 0x02;
        controller_pak_changed_[port] = false;
        valid = true;
        return;
    }
    if (command == 0x01) {
        u16 buttons = controller.buttons & 0xff3fU;
        // Opposing D-pad directions cancel independently on each axis.
        if ((buttons & 0x0c00U) == 0x0c00U)
            buttons &= 0xf3ffU;
        if ((buttons & 0x0300U) == 0x0300U)
            buttons &= 0xfcffU;
        // L+R+Start reports stick reset instead of Start while the chord is held.
        const bool reset_stick = (buttons & 0x1030U) == 0x1030U;
        if (reset_stick)
            buttons = static_cast<u16>((buttons & 0xefffU) | 0x0080U);
        output[0] = static_cast<u8>(buttons >> 8U);
        output[1] = static_cast<u8>(buttons);
        output[2] = reset_stick ? 0 : static_cast<u8>(controller.stick_x);
        output[3] = reset_stick ? 0 : static_cast<u8>(controller.stick_y);
        overflow = recv > 4;
        valid = true;
        return;
    }
    if (command == 0x02 && send >= 3 && recv >= 1) {
        const u16 encoded = static_cast<u16>((static_cast<u16>(input[1]) << 8U) | input[2]);
        const u16 address = encoded & 0xffe0U;
        const unsigned data_length = std::min<unsigned>(recv, 32);
        const bool accessible = (controller.accessory != ControllerAccessory::None) &&
                                !controller_pak_changed_[port] && (encoded & 0x1fU) == address_crc(address);
        for (unsigned index = 0; index < std::min<unsigned>(recv, 33); ++index)
            output[index] = 0;
        if (accessible) {
            for (unsigned index = 0; index < data_length; ++index) {
                const u32 pos = static_cast<u32>(address) + index;
                if (controller.accessory == ControllerAccessory::ControllerPak)
                    output[index] = pos < controller_paks[port].size() ? controller_paks[port][pos] : 0;
                else if (controller.accessory == ControllerAccessory::RumblePak)
                    output[index] = pos < 0x8000               ? 0
                                    : pos < 0x9000             ? 0x80
                                    : controller_rumble_[port] ? 0xff
                                                               : 0;
            }
        }
        valid = true;
        if (recv >= 33) {
            output[32] = pak_crc(output);
            if (!accessible)
                output[32] ^= 0xffU;
        }
        return;
    }
    if (command == 0x03 && send >= 4 && recv >= 1) {
        const u16 encoded = static_cast<u16>((static_cast<u16>(input[1]) << 8U) | input[2]);
        const u16 address = encoded & 0xffe0U;
        const unsigned data_length = std::min<unsigned>(send - 3U, 32);
        const bool accessible = (controller.accessory != ControllerAccessory::None) &&
                                !controller_pak_changed_[port] && (encoded & 0x1fU) == address_crc(address);
        if (accessible) {
            if (controller.accessory == ControllerAccessory::ControllerPak && address != 0x8000U) {
                for (unsigned index = 0; index < data_length; ++index) {
                    const u32 pos = static_cast<u32>(address) + index;
                    if (pos < controller_paks[port].size())
                        controller_paks[port][pos] = input[3 + index];
                }
            } else if (controller.accessory == ControllerAccessory::RumblePak && address >= 0xc000) {
                controller_rumble_[port] = (input[3] & 1U) != 0;
            }
        }
        output[0] = data_length == 32 ? pak_crc(input + 3) : 0;
        if (!accessible)
            output[0] ^= 0xffU;
        valid = true;
    }
    return;
}

} // namespace cupid
