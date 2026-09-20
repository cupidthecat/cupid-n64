#include "cupid/bus.hpp"

#include <algorithm>

namespace cupid {

void Bus::execute_eeprom(u8 send, u8 recv, const u8* input, u8* output, bool& valid) {
    if ((save_type != SaveType::Eeprom4K && save_type != SaveType::Eeprom16K) ||
        (eeprom.size() != 512 && eeprom.size() != 2048))
        return;
    const u8 command = input[0];
    if (command == 0x00 || command == 0xff) {
        std::fill_n(output, recv, u8{0});
        const std::array<u8, 3> status{0, eeprom.size() == 512 ? u8{0x80} : u8{0xc0},
                                       eeprom_busy_counter_ != 0 ? u8{0x80} : u8{0}};
        std::copy_n(status.begin(), std::min<std::size_t>(recv, status.size()), output);
        valid = true;
        return;
    }
    const std::size_t mask = eeprom.size() - 1;
    if (command == 0x04 && send >= 2) {
        const std::size_t address = static_cast<std::size_t>(input[1]) * 8;
        for (unsigned index = 0; index < recv; ++index)
            output[index] = eeprom_busy_counter_ == 0 ? eeprom[(address + index) & mask] : 0xff;
        valid = true;
        return;
    }
    if (command == 0x05 && send >= 2 && recv >= 1) {
        std::fill_n(output, recv, u8{0});
        output[0] = eeprom_busy_counter_ != 0 ? 0x80 : 0;
        valid = true;
        if (eeprom_busy_counter_ == 0) {
            const std::size_t address = static_cast<std::size_t>(input[1]) * 8;
            for (unsigned index = 0; index + 2U < send; ++index)
                eeprom[(address + index) & mask] = input[2 + index];
            eeprom_busy_counter_ = 375000;
        }
    }
}

void Bus::tick_eeprom(u64 cycles) {
    eeprom_busy_counter_ -= std::min(eeprom_busy_counter_, cycles);
}

} // namespace cupid
