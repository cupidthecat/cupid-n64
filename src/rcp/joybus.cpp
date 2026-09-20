#include "cupid/bus.hpp"

#include <algorithm>

namespace cupid {

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
    if (channel < 4)
        execute_controller(channel, send, recv, input, output, valid, overflow);

    if (channel == 4)
        execute_eeprom(send, recv, input, output, valid);
}

} // namespace cupid
