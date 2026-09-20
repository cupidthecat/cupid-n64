#include "cupid/bus.hpp"

#include <algorithm>

namespace cupid {

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

void Bus::process_pif() {
    if ((pif[0x7ff] & 0x02U) != 0) {
        std::span<u8, 15> challenge(pif.data() + 0x7c0U + 0x30, 15);
        cic.challenge(challenge);
        return;
    }

    u32 offset = 0;
    unsigned channel = 0;
    while (channel < 5 && offset < 63) {
        u8 send = pif[0x7c0U + offset++];
        if (send == 0xfe)
            break;
        if (send == 0xff)
            continue;
        if (send == 0 || send == 0xfd) {
            ++channel;
            continue;
        }
        if (offset >= 63)
            break;
        const u32 recv_offset = offset;
        u8 recv = pif[0x7c0U + offset++];
        const u8 flags = send & 0xc0U;
        send &= 0x3fU;
        recv &= 0x3fU;
        const u32 next = offset + send + recv;
        if (next >= 64U)
            break;
        if (flags != 0) {
            offset = next;
            ++channel;
            continue;
        }
        const u8* input = pif.data() + 0x7c0U + offset;
        std::array<u8, 64> output{};
        bool valid = false;
        bool overflow = false;
        execute_joybus(channel, send, recv, input, output.data(), valid, overflow);
        if (!valid)
            pif[0x7c0U + recv_offset] = static_cast<u8>(recv | 0x80U);
        else if (overflow)
            pif[0x7c0U + recv_offset] = static_cast<u8>(recv | 0x40U);
        else
            pif[0x7c0U + recv_offset] = recv;
        if (valid)
            std::copy_n(output.begin(), recv, pif.begin() + 0x7c0U + offset + send);
        offset = next;
        ++channel;
    }
    pif[0x7ff] &= static_cast<u8>(~1U);
}

void Bus::execute_joybus(unsigned channel, u8 send, u8 recv, const u8* input, u8* output, bool& valid,
                         bool& overflow) {
    overflow = false;
    if (send == 0)
        return;
    const u8 command = input[0];
    if (channel < 4) {
        const ControllerState& controller = controllers[channel];
        if (!controller.connected)
            return;
        if (command == 0x00 || command == 0xff) {
            output[0] = 0x05;
            output[1] = 0x00;
            output[2] = controller.controller_pak ? 0x01 : 0x02;
            valid = true;
            return;
        }
        if (command == 0x01) {
            output[0] = static_cast<u8>(controller.buttons >> 8U);
            output[1] = static_cast<u8>(controller.buttons);
            output[2] = static_cast<u8>(controller.stick_x);
            output[3] = static_cast<u8>(controller.stick_y);
            overflow = recv > 4;
            valid = true;
            return;
        }
        if (command == 0x02 && send >= 3 && recv >= 1) {
            const u16 encoded = static_cast<u16>((static_cast<u16>(input[1]) << 8U) | input[2]);
            const u16 address = encoded & 0xffe0U;
            const unsigned data_length = std::min<unsigned>(recv, 32);
            const bool accessible = controller.controller_pak && (encoded & 0x1fU) == address_crc(address);
            for (unsigned index = 0; index < std::min<unsigned>(recv, 33); ++index)
                output[index] = 0;
            if (accessible) {
                for (unsigned index = 0; index < data_length; ++index) {
                    const u32 pos = static_cast<u32>(address) + index;
                    output[index] = pos < controller_paks[channel].size() ? controller_paks[channel][pos] : 0;
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
            const bool accessible = controller.controller_pak && (encoded & 0x1fU) == address_crc(address);
            if (accessible && address != 0x8000U) {
                for (unsigned index = 0; index < data_length; ++index) {
                    const u32 pos = static_cast<u32>(address) + index;
                    if (pos < controller_paks[channel].size())
                        controller_paks[channel][pos] = input[3 + index];
                }
            }
            output[0] = data_length == 32 ? pak_crc(input + 3) : 0;
            if (!accessible)
                output[0] ^= 0xffU;
            valid = true;
        }
        return;
    }

    if (channel == 4)
        execute_eeprom(send, recv, input, output, valid);
}

} // namespace cupid
