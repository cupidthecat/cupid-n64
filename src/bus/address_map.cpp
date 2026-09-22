#include "cupid/system.hpp"

namespace cupid {

u64 Bus::read(u32 physical, unsigned width_bytes) {
    if (width_bytes != 1 && width_bytes != 2 && width_bytes != 4 && width_bytes != 8)
        return 0;

    if (physical < 0x04000000U) {
        const u64 value = read_rdram(physical, width_bytes);
        open_bus_ = static_cast<u32>(value);
        return value;
    }

    if (width_bytes == 8) {
        system_.cpu.frozen = true;
        return 0;
    }

    if (physical >= 0x04000000U && physical <= 0x0403ffffU)
        return read_sp_memory(physical, width_bytes);

    if (physical >= 0x1fc00000U && physical <= 0x1fcfffffU)
        return read_pif(physical, width_bytes);

    if ((physical >= 0x05000000U && physical <= 0x1fbfffffU) ||
        (physical >= 0x1fd00000U && physical <= 0x7fffffffU)) {
        return read_cart(physical, width_bytes);
    }

    if ((physical >= 0x04040000U && physical <= 0x040bffffU) ||
        (physical >= 0x04100000U && physical <= 0x048fffffU)) {
        const u32 word = read_rcp_word(physical & ~3U);
        open_bus_ = word;
        return extract_word_lane(word, physical, width_bytes);
    }

    system_.cpu.frozen = true;
    return 0;
}

void Bus::write(u32 physical, unsigned width_bytes, u64 value) {
    if (width_bytes != 1 && width_bytes != 2 && width_bytes != 4 && width_bytes != 8)
        return;

    if (physical < 0x04000000U) {
        write_rdram(physical, width_bytes, value);
        return;
    }

    if (physical >= 0x04000000U && physical <= 0x0403ffffU) {
        write_sp_memory(physical, width_bytes, value);
        return;
    }

    if (physical >= 0x1fc00000U && physical <= 0x1fcfffffU) {
        schedule_dirty_ = true;
        write_pif(physical, width_bytes, value);
        return;
    }

    if ((physical >= 0x05000000U && physical <= 0x1fbfffffU) ||
        (physical >= 0x1fd00000U && physical <= 0x7fffffffU)) {
        schedule_dirty_ = true;
        write_cart(physical, width_bytes, value);
        return;
    }

    if ((physical >= 0x04040000U && physical <= 0x040bffffU) ||
        (physical >= 0x04100000U && physical <= 0x048fffffU)) {
        schedule_dirty_ = true;
        const u32 word = expand_rcp_write(physical, width_bytes, value);
        write_rcp_word(physical & ~3U, word);
        return;
    }
    system_.cpu.frozen = true;
}

} // namespace cupid
