#include "cupid/cartridge/rtc.hpp"

#include <algorithm>

namespace cupid {
namespace {

unsigned decode(u8 value) {
    return (value >> 4) * 10U + (value & 15U);
}

u8 encode(unsigned value) {
    return static_cast<u8>(((value / 10) << 4) | (value % 10));
}

} // namespace

CartridgeRtc::CartridgeRtc(Registers registers) : registers_(registers) {}

void CartridgeRtc::reset_clock() {
    fraction_ = 0;
}

bool CartridgeRtc::execute(std::span<const u8> input, std::span<u8> output) {
    if (input.empty())
        return false;
    if (input[0] == 0x06 && output.size() >= 3) {
        std::fill(output.begin(), output.end(), u8{0});
        output[1] = 0x10;
        output[2] = status();
        return true;
    }
    if (input[0] == 0x07 && input.size() >= 2 && output.size() >= 9) {
        std::fill(output.begin(), output.end(), u8{0});
        std::copy_n(registers_.begin() + (input[1] & 3U) * 8, 8, output.begin());
        output[8] = status();
        return true;
    }
    if (input[0] == 0x08 && input.size() >= 10 && !output.empty()) {
        const unsigned block = input[1] & 3U;
        const bool locked =
            (block == 1 && (registers_[0] & 1U) != 0) || (block == 2 && (registers_[0] & 2U) != 0);
        if (!locked) {
            std::copy_n(input.begin() + 2, 8, registers_.begin() + block * 8);
            if (block == 0)
                reset_clock();
        }
        std::fill(output.begin(), output.end(), u8{0});
        output[0] = status();
        return true;
    }
    return false;
}

void CartridgeRtc::tick(u64 cycles) {
    if (!running())
        return;
    u64 seconds = cycles / cycles_per_second;
    fraction_ += cycles % cycles_per_second;
    if (fraction_ >= cycles_per_second) {
        fraction_ -= cycles_per_second;
        ++seconds;
    }
    while (seconds-- != 0)
        advance_second();
}

void CartridgeRtc::advance_second() {
    unsigned seconds = decode(registers_[16]);
    unsigned minutes = decode(registers_[17]);
    unsigned hours = decode(registers_[18] & 0x7fU);
    unsigned day = decode(registers_[19]);
    unsigned weekday = decode(registers_[20]);
    unsigned month = decode(registers_[21]);
    unsigned year = decode(registers_[22]) + 100 * decode(registers_[23]);
    if (++seconds == 60) {
        seconds = 0;
        if (++minutes == 60) {
            minutes = 0;
            if (++hours == 24) {
                hours = 0;
                if (++weekday == 7)
                    weekday = 0;
                unsigned days = 30 + ((month + (month >> 3)) & 1U);
                if (month == 2)
                    days = year % 4 == 0 ? 29U : 28U;
                if (++day > days) {
                    day = 1;
                    if (++month == 13) {
                        month = 1;
                        ++year;
                    }
                }
            }
        }
    }
    registers_[16] = encode(seconds);
    registers_[17] = encode(minutes);
    registers_[18] = encode(hours) | 0x80U;
    registers_[19] = encode(day);
    registers_[20] = encode(weekday);
    registers_[21] = encode(month);
    registers_[22] = encode(year % 100);
    registers_[23] = encode(year / 100);
}

} // namespace cupid
