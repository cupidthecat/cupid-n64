#include "cupid/cartridge/game_boy.hpp"

namespace cupid {

void GameBoyCartridge::set_clock(GameBoyClock clock) {
    clock.seconds &= 63;
    clock.minutes &= 63;
    clock.hours &= 31;
    clock.days &= 511;
    clock_ = clock;
    latched_ = clock;
    latch_value_ = 0;
    fraction_ = 0;
}

u8 GameBoyCartridge::read_clock() const {
    switch (ram_bank_) {
    case 8:
        return latched_.seconds;
    case 9:
        return latched_.minutes;
    case 10:
        return latched_.hours;
    case 11:
        return static_cast<u8>(latched_.days);
    default:
        return static_cast<u8>((latched_.days >> 8U) | (latched_.halted ? 64U : 0U) |
                               (latched_.carry ? 128U : 0U));
    }
}

void GameBoyCartridge::write_clock(u8 value) {
    switch (ram_bank_) {
    case 8:
        clock_.seconds = value & 63U;
        break;
    case 9:
        clock_.minutes = value & 63U;
        break;
    case 10:
        clock_.hours = value & 31U;
        break;
    case 11:
        clock_.days = static_cast<u16>((clock_.days & 256U) | value);
        break;
    case 12:
        clock_.days = static_cast<u16>((clock_.days & 255U) | ((value & 1U) << 8U));
        clock_.halted = (value & 64U) != 0;
        clock_.carry = (value & 128U) != 0;
        break;
    }
}

void GameBoyCartridge::advance_seconds(u64 seconds) {
    const auto advance = [](u8& value, unsigned limit, unsigned modulus, u64 count) -> u64 {
        if (value >= limit) {
            const unsigned until_wrap = modulus - value;
            if (count < until_wrap) {
                value = static_cast<u8>(value + count);
                return 0;
            }
            count -= until_wrap;
            value = 0;
        }
        const u64 total = value + count;
        value = static_cast<u8>(total % limit);
        return total / limit;
    };
    const u64 minutes = advance(clock_.seconds, 60, 64, seconds);
    const u64 hours = advance(clock_.minutes, 60, 64, minutes);
    const u64 days = clock_.days + advance(clock_.hours, 24, 32, hours);
    if (days >= 512)
        clock_.carry = true;
    clock_.days = static_cast<u16>(days & 511U);
}

void GameBoyCartridge::tick(u64 cycles) {
    if (!clock_running())
        return;
    u64 seconds = cycles / cycles_per_second;
    fraction_ += cycles % cycles_per_second;
    if (fraction_ >= cycles_per_second) {
        fraction_ -= cycles_per_second;
        ++seconds;
    }
    advance_seconds(seconds);
}

} // namespace cupid
