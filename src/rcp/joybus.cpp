#include "cupid/bus.hpp"

#include <algorithm>

namespace cupid {

void Bus::process_pif() {
    if ((pif[0x7ff] & 0x02U) != 0) {
        std::span<u8, 15> challenge(pif.data() + 0x7c0U + 0x30, 15);
        cic.challenge(challenge);
        return;
    }

    joybus.execute();
}

void Joybus::reset() {
    channels_ = {};
}

void Joybus::configure() {
    reset();
    const auto& pif = bus_.pif;
    u32 offset = 0;
    unsigned channel = 0;
    while (channel < 5 && offset < 63) {
        u8 send = pif[0x7c0U + offset++];
        if (send == 0xfe)
            break;
        if (send == 0xff)
            continue;
        if (send == 0 || send == 0xfd) {
            channels_[channel].reset = send == 0xfd;
            ++channel;
            continue;
        }
        if (offset >= 63)
            break;
        u8 recv = pif[0x7c0U + offset++];
        send &= 0x3fU;
        recv &= 0x3fU;
        const u32 next = offset + send + recv;
        if (next >= 64U)
            break;
        channels_[channel++] = {static_cast<u8>(offset - 2), false, false};
        offset = next;
    }
}

void Joybus::execute() {
    auto& pif = bus_.pif;
    for (unsigned remaining = 5; remaining != 0; --remaining) {
        const unsigned channel = remaining - 1;
        const auto descriptor = channels_[channel];
        if (descriptor.skip || descriptor.reset)
            continue;
        u32 offset = descriptor.offset;
        const u8 raw_send = pif[0x7c0U + offset++];
        if ((raw_send & 0xc0U) != 0)
            continue;
        const u32 recv_offset = offset;
        const u8 send = raw_send & 0x3fU;
        const u8 recv = pif[0x7c0U + offset++] & 0x3fU;
        if (offset + send + recv >= 64)
            continue;
        const u8* input = pif.data() + 0x7c0U + offset;
        std::array<u8, 64> output{};
        bool valid = false;
        bool overflow = false;
        bus_.execute_joybus(channel, send, recv, input, output.data(), valid, overflow);
        if (!valid)
            pif[0x7c0U + recv_offset] = static_cast<u8>(recv | 0x80U);
        else if (overflow)
            pif[0x7c0U + recv_offset] = static_cast<u8>(recv | 0x40U);
        else
            pif[0x7c0U + recv_offset] = recv;
        if (valid)
            std::copy_n(output.begin(), recv, pif.begin() + 0x7c0U + offset + send);
    }
}

void Bus::execute_joybus(unsigned channel, u8 send, u8 recv, const u8* input, u8* output, bool& valid,
                         bool& overflow) {
    overflow = false;
    if (send == 0)
        return;
    if (channel < 4)
        execute_controller(channel, send, recv, input, output, valid, overflow);

    if (channel == 4) {
        execute_eeprom(send, recv, input, output, valid);
        if (!valid && rtc)
            valid = rtc->execute({input, send}, {output, recv});
    }
}

} // namespace cupid
