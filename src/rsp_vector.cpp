#include "cupid/rsp.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace cupid {

namespace {

constexpr u64 acc_mask = (u64{1} << 48) - 1;
constexpr u64 acc_sign = u64{1} << 47;

constexpr s8 immediate7(u32 instruction) {
    u8 value = static_cast<u8>(instruction & 0x7f);
    if (value & 0x40) {
        value |= 0x80;
    }
    return std::bit_cast<s8>(value);
}

constexpr u16 bits16(s32 value) {
    return static_cast<u16>(static_cast<u32>(value));
}

} // namespace

u16 Rsp::vec_u16(const Vector& vector, unsigned lane) {
    const unsigned offset = (lane & 7) * 2;
    return static_cast<u16>((static_cast<u16>(vector.byte[offset]) << 8) | vector.byte[offset + 1]);
}

s16 Rsp::vec_s16(const Vector& vector, unsigned lane) {
    return std::bit_cast<s16>(vec_u16(vector, lane));
}

void Rsp::vec_set_u16(Vector& vector, unsigned lane, u16 value) {
    const unsigned offset = (lane & 7) * 2;
    vector.byte[offset] = static_cast<u8>(value >> 8);
    vector.byte[offset + 1] = static_cast<u8>(value);
}

void Rsp::vec_set_s16(Vector& vector, unsigned lane, s16 value) {
    vec_set_u16(vector, lane, std::bit_cast<u16>(value));
}

unsigned Rsp::element_lane(unsigned element, unsigned lane) {
    lane &= 7;
    switch (element & 15) {
    case 0:
    case 1:
        return lane;
    case 2:
        return lane & ~1u;
    case 3:
        return lane | 1u;
    case 4:
        return lane < 4 ? 0 : 4;
    case 5:
        return lane < 4 ? 1 : 5;
    case 6:
        return lane < 4 ? 2 : 6;
    case 7:
        return lane < 4 ? 3 : 7;
    default:
        return (element & 15) - 8;
    }
}

u16 Rsp::selected_u16(const Vector& vector, unsigned element, unsigned lane) const {
    return vec_u16(vector, element_lane(element, lane));
}

s16 Rsp::selected_s16(const Vector& vector, unsigned element, unsigned lane) const {
    return std::bit_cast<s16>(selected_u16(vector, element, lane));
}

s16 Rsp::clamp_s16(s64 value) {
    if (value > std::numeric_limits<s16>::max()) {
        return std::numeric_limits<s16>::max();
    }
    if (value < std::numeric_limits<s16>::min()) {
        return std::numeric_limits<s16>::min();
    }
    return static_cast<s16>(value);
}

s64 Rsp::wrap_accumulator(s64 value) {
    u64 bits = static_cast<u64>(value) & acc_mask;
    if (bits & acc_sign) {
        bits |= ~acc_mask;
    }
    return std::bit_cast<s64>(bits);
}

void Rsp::set_accumulator(unsigned lane, s64 value) {
    accumulator_[lane & 7] = wrap_accumulator(value);
}

u16 Rsp::acc_low(unsigned lane) const {
    return static_cast<u16>(static_cast<u64>(accumulator_[lane & 7]));
}

u16 Rsp::acc_mid(unsigned lane) const {
    return static_cast<u16>(static_cast<u64>(accumulator_[lane & 7]) >> 16);
}

u16 Rsp::acc_high(unsigned lane) const {
    return static_cast<u16>(static_cast<u64>(accumulator_[lane & 7]) >> 32);
}

s16 Rsp::acc_mid_s(unsigned lane) const {
    return std::bit_cast<s16>(acc_mid(lane));
}

s16 Rsp::acc_high_s(unsigned lane) const {
    return std::bit_cast<s16>(acc_high(lane));
}

void Rsp::set_acc_low(unsigned lane, u16 value) {
    const u64 old = static_cast<u64>(accumulator_[lane & 7]) & acc_mask;
    set_accumulator(lane, static_cast<s64>((old & ~u64{0xffff}) | value));
}

void Rsp::set_acc_mid(unsigned lane, u16 value) {
    const u64 old = static_cast<u64>(accumulator_[lane & 7]) & acc_mask;
    const u64 updated = (old & ~(u64{0xffff} << 16)) | (static_cast<u64>(value) << 16);
    set_accumulator(lane, static_cast<s64>(updated));
}

void Rsp::set_acc_high(unsigned lane, u16 value) {
    const u64 old = static_cast<u64>(accumulator_[lane & 7]) & acc_mask;
    const u64 updated = (old & ~(u64{0xffff} << 32)) | (static_cast<u64>(value) << 32);
    set_accumulator(lane, static_cast<s64>(updated));
}

u16 Rsp::saturate_accumulator(unsigned lane, bool middle_slice, u16 negative, u16 positive) const {
    if (acc_high_s(lane) < 0) {
        if (acc_high(lane) != 0xffff || acc_mid_s(lane) >= 0) {
            return negative;
        }
    } else {
        if (acc_high(lane) != 0 || acc_mid_s(lane) < 0) {
            return positive;
        }
    }
    return middle_slice ? acc_mid(lane) : acc_low(lane);
}

bool Rsp::flag(u8 mask, unsigned lane) const {
    return ((mask >> (lane & 7)) & 1) != 0;
}

void Rsp::set_flag(u8& mask, unsigned lane, bool value) {
    const u8 bit = static_cast<u8>(1u << (lane & 7));
    mask = value ? static_cast<u8>(mask | bit) : static_cast<u8>(mask & ~bit);
}

u32 Rsp::reciprocal(u32 value) {
    if (value == 0) {
        return 0x7fff'ffff;
    }
    if (value == 0xffff'8000) {
        return 0xffff'0000;
    }

    const u32 adjusted = value > 0xffff'8000 ? value - 1 : value;
    const bool negative = static_cast<s32>(adjusted) < 0;
    const u32 positive = negative ? ~adjusted : adjusted;
    const unsigned shift = static_cast<unsigned>(std::countl_zero(positive)) + 1u;
    const u32 normalized = positive << (shift & 31);
    const unsigned index = normalized >> 23;

    u16 table = 0xffff;
    if (index != 0) {
        const u64 quotient = (u64{1} << 34) / (index + 512);
        table = static_cast<u16>((quotient + 1) >> 8);
    }

    const u32 magnitude = (0x4000'0000u | (static_cast<u32>(table) << 14)) >> (32 - shift);
    return negative ? ~magnitude : magnitude;
}

u32 Rsp::reciprocal_sqrt(u32 value) {
    if (value == 0) {
        return 0x7fff'ffff;
    }
    if (value == 0xffff'8000) {
        return 0xffff'0000;
    }

    const u32 adjusted = value > 0xffff'8000 ? value - 1 : value;
    const bool negative = static_cast<s32>(adjusted) < 0;
    const u32 positive = negative ? ~adjusted : adjusted;
    const unsigned shift = static_cast<unsigned>(std::countl_zero(positive)) + 1u;
    const u32 normalized = positive << (shift & 31);
    const unsigned index = (normalized >> 24) | ((shift & 1) << 8);
    const u64 a = index < 256 ? index + 256 : ((index - 256) << 1) + 512;

    u64 b = u64{1} << 17;
    u64 increment = 512;
    while (increment != 0) {
        while (a * (b + increment) * (b + increment) < (u64{1} << 44)) {
            b += increment;
        }
        increment >>= 1;
    }
    const u16 table = static_cast<u16>(b >> 1);
    const u32 magnitude = (0x4000'0000u | (static_cast<u32>(table) << 14)) >> ((32 - shift) >> 1);
    return negative ? ~magnitude : magnitude;
}

void Rsp::execute_cop2(u32 instruction) {
    const unsigned rs = (instruction >> 21) & 31;
    if (rs >= 16) {
        execute_vector_op(instruction);
        return;
    }

    const unsigned rt = (instruction >> 16) & 31;
    const unsigned rd = (instruction >> 11) & 31;
    const unsigned element = (instruction >> 7) & 15;

    switch (rs) {
    case 0x00: {
        const u16 value = static_cast<u16>((static_cast<u16>(vr_[rd].byte[element]) << 8) |
                                           vr_[rd].byte[(element + 1) & 15]);
        write_gpr(rt, static_cast<u32>(static_cast<s32>(std::bit_cast<s16>(value))));
        break;
    }
    case 0x02: {
        u16 value = 0;
        switch (rd & 3) {
        case 0:
            value = static_cast<u16>(vcol_ | (static_cast<u16>(vcoh_) << 8));
            break;
        case 1:
            value = static_cast<u16>(vccl_ | (static_cast<u16>(vcch_) << 8));
            break;
        default:
            value = vce_;
            break;
        }
        write_gpr(rt, static_cast<u32>(static_cast<s32>(std::bit_cast<s16>(value))));
        break;
    }
    case 0x04:
        vr_[rd].byte[element] = static_cast<u8>(gpr_[rt] >> 8);
        if (element != 15) {
            vr_[rd].byte[element + 1] = static_cast<u8>(gpr_[rt]);
        }
        break;
    case 0x06:
        switch (rd & 3) {
        case 0:
            vcol_ = static_cast<u8>(gpr_[rt]);
            vcoh_ = static_cast<u8>(gpr_[rt] >> 8);
            break;
        case 1:
            vccl_ = static_cast<u8>(gpr_[rt]);
            vcch_ = static_cast<u8>(gpr_[rt] >> 8);
            break;
        default:
            vce_ = static_cast<u8>(gpr_[rt]);
            break;
        }
        break;
    default:
        break;
    }
}

void Rsp::execute_vector_load(u32 instruction) {
    const unsigned base_reg = (instruction >> 21) & 31;
    const unsigned vt_index = (instruction >> 16) & 31;
    const unsigned kind = (instruction >> 11) & 31;
    const unsigned element = (instruction >> 7) & 15;
    const s32 imm = immediate7(instruction);
    Vector& vt = vr_[vt_index];

    const auto address_for = [&](s32 scale) { return gpr_[base_reg] + static_cast<u32>(imm * scale); };

    switch (kind) {
    case 0x00:
        vt.byte[element] = dmem_read8(address_for(1));
        break;
    case 0x01: {
        u32 address = address_for(2);
        for (unsigned byte = element; byte < std::min(element + 2, 16u); ++byte) {
            vt.byte[byte] = dmem_read8(address++);
        }
        break;
    }
    case 0x02: {
        u32 address = address_for(4);
        for (unsigned byte = element; byte < std::min(element + 4, 16u); ++byte) {
            vt.byte[byte] = dmem_read8(address++);
        }
        break;
    }
    case 0x03: {
        u32 address = address_for(8);
        for (unsigned byte = element; byte < std::min(element + 8, 16u); ++byte) {
            vt.byte[byte] = dmem_read8(address++);
        }
        break;
    }
    case 0x04: {
        u32 address = address_for(16);
        const unsigned end = std::min(16u + element - (address & 15), 16u);
        for (unsigned byte = element; byte < end; ++byte) {
            vt.byte[byte] = dmem_read8(address++);
        }
        break;
    }
    case 0x05: {
        u32 address = address_for(16);
        const unsigned base = address & 15;
        const int start = 16 - (static_cast<int>(base) - static_cast<int>(element));
        address &= ~15u;
        for (int byte = start; byte < 16; ++byte) {
            if (byte >= 0) {
                vt.byte[static_cast<unsigned>(byte) & 15] = dmem_read8(address++);
            }
        }
        break;
    }
    case 0x06: {
        u32 address = address_for(8);
        const u32 index = (address & 7) - element;
        address &= ~7u;
        for (unsigned lane = 0; lane < 8; ++lane) {
            vec_set_u16(vt, lane,
                        static_cast<u16>(static_cast<u16>(dmem_read8(address + ((index + lane) & 15))) << 8));
        }
        break;
    }
    case 0x07: {
        u32 address = address_for(8);
        const u32 index = (address & 7) - element;
        address &= ~7u;
        for (unsigned lane = 0; lane < 8; ++lane) {
            vec_set_u16(vt, lane,
                        static_cast<u16>(static_cast<u16>(dmem_read8(address + ((index + lane) & 15))) << 7));
        }
        break;
    }
    case 0x08: {
        u32 address = address_for(16);
        const u32 index = (address & 7) - element;
        address &= ~7u;
        for (unsigned lane = 0; lane < 8; ++lane) {
            vec_set_u16(
                vt, lane,
                static_cast<u16>(static_cast<u16>(dmem_read8(address + ((index + lane * 2) & 15))) << 7));
        }
        break;
    }
    case 0x09: {
        u32 address = address_for(16);
        const u32 index = (address & 7) - element;
        address &= ~7u;
        Vector temporary{};
        for (unsigned lane = 0; lane < 4; ++lane) {
            vec_set_u16(
                temporary, lane,
                static_cast<u16>(static_cast<u16>(dmem_read8(address + ((index + lane * 4) & 15))) << 7));
            vec_set_u16(
                temporary, lane + 4,
                static_cast<u16>(static_cast<u16>(dmem_read8(address + ((index + lane * 4 + 8) & 15))) << 7));
        }
        for (unsigned byte = element; byte < std::min(element + 8, 16u); ++byte) {
            vt.byte[byte] = temporary.byte[byte];
        }
        break;
    }
    case 0x0b: {
        u32 address = address_for(16);
        const u32 begin = address & ~7u;
        address = begin + ((element + (address & 8)) & 15);
        const unsigned register_base = vt_index & ~7u;
        unsigned register_offset = element >> 1;
        for (unsigned lane = 0; lane < 8; ++lane) {
            Vector& target = vr_[register_base + register_offset];
            target.byte[lane * 2] = dmem_read8(address++);
            if (address == begin + 16)
                address = begin;
            target.byte[lane * 2 + 1] = dmem_read8(address++);
            if (address == begin + 16)
                address = begin;
            register_offset = (register_offset + 1) & 7;
        }
        break;
    }
    default:
        break;
    }
}

void Rsp::execute_vector_store(u32 instruction) {
    const unsigned base_reg = (instruction >> 21) & 31;
    const unsigned vt_index = (instruction >> 16) & 31;
    const unsigned kind = (instruction >> 11) & 31;
    const unsigned element = (instruction >> 7) & 15;
    const s32 imm = immediate7(instruction);
    const Vector& vt = vr_[vt_index];

    const auto address_for = [&](s32 scale) { return gpr_[base_reg] + static_cast<u32>(imm * scale); };

    switch (kind) {
    case 0x00:
        dmem_write8(address_for(1), vt.byte[element]);
        break;
    case 0x01: {
        u32 address = address_for(2);
        for (unsigned byte = element; byte < element + 2; ++byte) {
            dmem_write8(address++, vt.byte[byte & 15]);
        }
        break;
    }
    case 0x02: {
        u32 address = address_for(4);
        for (unsigned byte = element; byte < element + 4; ++byte) {
            dmem_write8(address++, vt.byte[byte & 15]);
        }
        break;
    }
    case 0x03: {
        u32 address = address_for(8);
        for (unsigned byte = element; byte < element + 8; ++byte) {
            dmem_write8(address++, vt.byte[byte & 15]);
        }
        break;
    }
    case 0x04: {
        u32 address = address_for(16);
        const unsigned end = element + (16 - (address & 15));
        for (unsigned byte = element; byte < end; ++byte) {
            dmem_write8(address++, vt.byte[byte & 15]);
        }
        break;
    }
    case 0x05: {
        u32 address = address_for(16);
        const unsigned end = element + (address & 15);
        const unsigned source_base = 16 - (address & 15);
        address &= ~15u;
        for (unsigned byte = element; byte < end; ++byte) {
            dmem_write8(address++, vt.byte[(byte + source_base) & 15]);
        }
        break;
    }
    case 0x06: {
        u32 address = address_for(8);
        for (unsigned byte = element; byte < element + 8; ++byte) {
            const unsigned index = byte & 15;
            if (index < 8) {
                dmem_write8(address++, vt.byte[(index & 7) << 1]);
            } else {
                dmem_write8(address++, static_cast<u8>(vec_u16(vt, index & 7) >> 7));
            }
        }
        break;
    }
    case 0x07: {
        u32 address = address_for(8);
        for (unsigned byte = element; byte < element + 8; ++byte) {
            const unsigned index = byte & 15;
            if (index < 8) {
                dmem_write8(address++, static_cast<u8>(vec_u16(vt, index & 7) >> 7));
            } else {
                dmem_write8(address++, vt.byte[(index & 7) << 1]);
            }
        }
        break;
    }
    case 0x08: {
        u32 address = address_for(16);
        const u32 index = address & 7;
        address &= ~7u;
        for (unsigned lane = 0; lane < 8; ++lane) {
            const unsigned byte = element + lane * 2;
            const u8 value = static_cast<u8>((static_cast<u16>(vt.byte[byte & 15]) << 1) |
                                             (vt.byte[(byte + 1) & 15] >> 7));
            dmem_write8(address + ((index + lane * 2) & 15), value);
        }
        break;
    }
    case 0x09: {
        u32 address = address_for(16);
        const u32 base = address & 7;
        address &= ~7u;
        std::array<unsigned, 4> lanes{};
        switch (element) {
        case 0:
        case 15:
            lanes = {0, 1, 2, 3};
            break;
        case 1:
            lanes = {6, 7, 4, 5};
            break;
        case 4:
            lanes = {1, 2, 3, 0};
            break;
        case 5:
            lanes = {7, 4, 5, 6};
            break;
        case 8:
            lanes = {4, 5, 6, 7};
            break;
        case 11:
            lanes = {3, 0, 1, 2};
            break;
        case 12:
            lanes = {5, 6, 7, 4};
            break;
        default:
            for (unsigned i = 0; i < 4; ++i) {
                dmem_write8(address + ((base + i * 4) & 15), 0);
            }
            return;
        }
        for (unsigned i = 0; i < 4; ++i) {
            dmem_write8(address + ((base + i * 4) & 15), static_cast<u8>(vec_u16(vt, lanes[i]) >> 7));
        }
        break;
    }
    case 0x0a: {
        u32 address = address_for(16);
        u32 base = address & 7;
        address &= ~7u;
        for (unsigned byte = element; byte < element + 16; ++byte) {
            dmem_write8(address + (base & 15), vt.byte[byte & 15]);
            ++base;
        }
        break;
    }
    case 0x0b: {
        u32 address = address_for(16);
        const unsigned register_base = vt_index & ~7u;
        unsigned vector_byte = 16 - (element & ~1u);
        u32 memory_offset = (address & 7) - (element & ~1u);
        address &= ~7u;
        for (unsigned reg = register_base; reg < register_base + 8; ++reg) {
            dmem_write8(address + (memory_offset++ & 15), vr_[reg].byte[vector_byte++ & 15]);
            dmem_write8(address + (memory_offset++ & 15), vr_[reg].byte[vector_byte++ & 15]);
        }
        break;
    }
    default:
        break;
    }
}

void Rsp::execute_vector_op(u32 instruction) {
    const unsigned element = (instruction >> 21) & 15;
    const unsigned vt_index = (instruction >> 16) & 31;
    const unsigned vs_index = (instruction >> 11) & 31;
    const unsigned vd_index = (instruction >> 6) & 31;
    const unsigned de = (instruction >> 11) & 7;
    const unsigned function = instruction & 63;
    const Vector vs = vr_[vs_index];
    const Vector vt = vr_[vt_index];
    Vector& vd = vr_[vd_index];

    const auto copy_selected_to_acc_low = [&] {
        for (unsigned lane = 0; lane < 8; ++lane) {
            set_acc_low(lane, selected_u16(vt, element, lane));
        }
    };

    switch (function) {
    case 0x00:
    case 0x01: {
        const bool unsigned_result = function == 0x01;
        for (unsigned lane = 0; lane < 8; ++lane) {
            const s64 product = static_cast<s64>(vec_s16(vs, lane)) * selected_s16(vt, element, lane);
            set_accumulator(lane, product * 2 + 0x8000);
            if (!unsigned_result) {
                vec_set_u16(vd, lane, saturate_accumulator(lane, true, 0x8000, 0x7fff));
            } else if (acc_high_s(lane) < 0) {
                vec_set_u16(vd, lane, 0);
            } else if (acc_mid_s(lane) < 0) {
                vec_set_u16(vd, lane, 0xffff);
            } else {
                vec_set_u16(vd, lane, acc_mid(lane));
            }
        }
        break;
    }
    case 0x02:
    case 0x0a: {
        const bool positive = function == 0x02;
        for (unsigned lane = 0; lane < 8; ++lane) {
            s64 product = selected_s16(vt, element, lane);
            if (vs_index & 1)
                product *= 0x1'0000;
            s64 acc = accumulator_[lane];
            if ((!positive && acc < 0) || (positive && acc >= 0)) {
                acc = wrap_accumulator(acc + product);
                set_accumulator(lane, acc);
            }
            vec_set_s16(vd, lane, clamp_s16(accumulator_[lane] >> 16));
        }
        break;
    }
    case 0x03:
        for (unsigned lane = 0; lane < 8; ++lane) {
            s32 product =
                static_cast<s32>(vec_s16(vs, lane)) * static_cast<s32>(selected_s16(vt, element, lane));
            if (product < 0)
                product += 31;
            set_acc_high(lane, static_cast<u16>(static_cast<u32>(product) >> 16));
            set_acc_mid(lane, static_cast<u16>(product));
            set_acc_low(lane, 0);
            vec_set_s16(
                vd, lane,
                static_cast<s16>(static_cast<u16>(clamp_s16(static_cast<s64>(product) >> 1)) & 0xfff0));
        }
        break;
    case 0x04:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const u32 product = static_cast<u32>(vec_u16(vs, lane)) * selected_u16(vt, element, lane);
            set_accumulator(lane, static_cast<s64>(product >> 16));
            vec_set_u16(vd, lane, acc_low(lane));
        }
        break;
    case 0x05:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const s64 product = static_cast<s64>(vec_s16(vs, lane)) * selected_u16(vt, element, lane);
            set_accumulator(lane, product);
            vec_set_u16(vd, lane, acc_mid(lane));
        }
        break;
    case 0x06:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const s64 product = static_cast<s64>(vec_u16(vs, lane)) * selected_s16(vt, element, lane);
            set_accumulator(lane, product);
            vec_set_u16(vd, lane, acc_low(lane));
        }
        break;
    case 0x07:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const s64 product = static_cast<s64>(vec_s16(vs, lane)) * selected_s16(vt, element, lane);
            set_accumulator(lane, product * 0x1'0000);
            vec_set_u16(vd, lane, saturate_accumulator(lane, true, 0x8000, 0x7fff));
        }
        break;
    case 0x08:
    case 0x09: {
        const bool unsigned_result = function == 0x09;
        for (unsigned lane = 0; lane < 8; ++lane) {
            const s64 product = static_cast<s64>(vec_s16(vs, lane)) * selected_s16(vt, element, lane) * 2;
            set_accumulator(lane, accumulator_[lane] + product);
            if (!unsigned_result) {
                vec_set_u16(vd, lane, saturate_accumulator(lane, true, 0x8000, 0x7fff));
            } else if (acc_high_s(lane) < 0) {
                vec_set_u16(vd, lane, 0);
            } else if (acc_high(lane) != 0 || acc_mid_s(lane) < 0) {
                vec_set_u16(vd, lane, 0xffff);
            } else {
                vec_set_u16(vd, lane, acc_mid(lane));
            }
        }
        break;
    }
    case 0x0b:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const u32 product_bits = (static_cast<u32>(acc_high(lane)) << 16) | acc_mid(lane);
            s32 product = std::bit_cast<s32>(product_bits);
            if (product < 0 && !(static_cast<u32>(product) & 0x20)) {
                product += 32;
            } else if (product >= 32 && !(static_cast<u32>(product) & 0x20)) {
                product -= 32;
            }
            set_acc_high(lane, static_cast<u16>(static_cast<u32>(product) >> 16));
            set_acc_mid(lane, static_cast<u16>(product));
            vec_set_s16(
                vd, lane,
                static_cast<s16>(static_cast<u16>(clamp_s16(static_cast<s64>(product) >> 1)) & 0xfff0));
        }
        break;
    case 0x0c:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const u32 product = static_cast<u32>(vec_u16(vs, lane)) * selected_u16(vt, element, lane);
            set_accumulator(lane, accumulator_[lane] + (product >> 16));
            vec_set_u16(vd, lane, saturate_accumulator(lane, false, 0, 0xffff));
        }
        break;
    case 0x0d:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const s64 product = static_cast<s64>(vec_s16(vs, lane)) * selected_u16(vt, element, lane);
            set_accumulator(lane, accumulator_[lane] + product);
            vec_set_u16(vd, lane, saturate_accumulator(lane, true, 0x8000, 0x7fff));
        }
        break;
    case 0x0e:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const s64 product = static_cast<s64>(vec_u16(vs, lane)) * selected_s16(vt, element, lane);
            set_accumulator(lane, accumulator_[lane] + product);
            vec_set_u16(vd, lane, saturate_accumulator(lane, false, 0, 0xffff));
        }
        break;
    case 0x0f:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const s64 upper = accumulator_[lane] >> 16;
            const s64 product = static_cast<s64>(vec_s16(vs, lane)) * selected_s16(vt, element, lane);
            const s64 result = upper + product;
            set_acc_high(lane, static_cast<u16>(static_cast<u64>(result) >> 16));
            set_acc_mid(lane, static_cast<u16>(result));
            vec_set_u16(vd, lane, saturate_accumulator(lane, true, 0x8000, 0x7fff));
        }
        break;
    case 0x10:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const s32 result = static_cast<s32>(vec_s16(vs, lane)) + selected_s16(vt, element, lane) +
                               (flag(vcol_, lane) ? 1 : 0);
            set_acc_low(lane, bits16(result));
            vec_set_s16(vd, lane, clamp_s16(result));
        }
        vcol_ = vcoh_ = 0;
        break;
    case 0x11:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const s32 result = static_cast<s32>(vec_s16(vs, lane)) - selected_s16(vt, element, lane) -
                               (flag(vcol_, lane) ? 1 : 0);
            set_acc_low(lane, bits16(result));
            vec_set_s16(vd, lane, clamp_s16(result));
        }
        vcol_ = vcoh_ = 0;
        break;
    case 0x12:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1a:
    case 0x1b:
    case 0x1c:
    case 0x1e:
    case 0x1f:
    case 0x2e:
    case 0x2f:
    case 0x38:
    case 0x39:
    case 0x3a:
    case 0x3b:
    case 0x3c:
    case 0x3d:
    case 0x3e:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const s32 result = static_cast<s32>(vec_s16(vs, lane)) + selected_s16(vt, element, lane);
            set_acc_low(lane, bits16(result));
            vec_set_u16(vd, lane, 0);
        }
        break;
    case 0x13:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const s16 control = vec_s16(vs, lane);
            const s16 input = selected_s16(vt, element, lane);
            if (control < 0) {
                if (input == std::numeric_limits<s16>::min()) {
                    set_acc_low(lane, 0x8000);
                    vec_set_u16(vd, lane, 0x7fff);
                } else {
                    const s16 result = static_cast<s16>(-input);
                    set_acc_low(lane, std::bit_cast<u16>(result));
                    vec_set_s16(vd, lane, result);
                }
            } else if (control > 0) {
                set_acc_low(lane, std::bit_cast<u16>(input));
                vec_set_s16(vd, lane, input);
            } else {
                set_acc_low(lane, 0);
                vec_set_u16(vd, lane, 0);
            }
        }
        break;
    case 0x14:
        vcoh_ = 0;
        for (unsigned lane = 0; lane < 8; ++lane) {
            const u32 result = static_cast<u32>(vec_u16(vs, lane)) + selected_u16(vt, element, lane);
            set_acc_low(lane, static_cast<u16>(result));
            set_flag(vcol_, lane, result > 0xffff);
            vec_set_u16(vd, lane, static_cast<u16>(result));
        }
        break;
    case 0x15:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const u16 left = vec_u16(vs, lane);
            const u16 right = selected_u16(vt, element, lane);
            const u16 result = static_cast<u16>(left - right);
            set_acc_low(lane, result);
            set_flag(vcol_, lane, left < right);
            set_flag(vcoh_, lane, result != 0);
            vec_set_u16(vd, lane, result);
        }
        break;
    case 0x1d:
        for (unsigned lane = 0; lane < 8; ++lane) {
            u16 value = 0;
            if (element == 8)
                value = acc_high(lane);
            if (element == 9)
                value = acc_mid(lane);
            if (element == 10)
                value = acc_low(lane);
            vec_set_u16(vd, lane, value);
        }
        break;
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23: {
        vcch_ = 0;
        const u8 old_vcol = vcol_;
        const u8 old_vcoh = vcoh_;
        for (unsigned lane = 0; lane < 8; ++lane) {
            const s16 left = vec_s16(vs, lane);
            const s16 right = selected_s16(vt, element, lane);
            bool choose_left = false;
            if (function == 0x20) {
                choose_left = left < right || (left == right && flag(old_vcol, lane) && flag(old_vcoh, lane));
            } else if (function == 0x21) {
                choose_left = left == right && !flag(old_vcoh, lane);
            } else if (function == 0x22) {
                choose_left = left != right || flag(old_vcoh, lane);
            } else {
                choose_left =
                    left > right || (left == right && (!flag(old_vcol, lane) || !flag(old_vcoh, lane)));
            }
            set_flag(vccl_, lane, choose_left);
            const u16 result = choose_left ? vec_u16(vs, lane) : selected_u16(vt, element, lane);
            set_acc_low(lane, result);
            vec_set_u16(vd, lane, result);
        }
        vcol_ = vcoh_ = 0;
        break;
    }
    case 0x24: {
        const u8 old_vcol = vcol_;
        const u8 old_vcoh = vcoh_;
        for (unsigned lane = 0; lane < 8; ++lane) {
            const u16 left = vec_u16(vs, lane);
            const u16 right = selected_u16(vt, element, lane);
            u16 result = left;
            if (flag(old_vcol, lane)) {
                if (!flag(old_vcoh, lane)) {
                    const u32 full = static_cast<u32>(left) + right;
                    const u16 sum = static_cast<u16>(full);
                    const bool carry = full > 0xffff;
                    const bool select = flag(vce_, lane) ? (sum == 0 || !carry) : (sum == 0 && !carry);
                    set_flag(vccl_, lane, select);
                }
                result = flag(vccl_, lane) ? static_cast<u16>(0u - right) : left;
            } else {
                if (!flag(old_vcoh, lane)) {
                    set_flag(vcch_, lane, left >= right);
                }
                result = flag(vcch_, lane) ? right : left;
            }
            set_acc_low(lane, result);
            vec_set_u16(vd, lane, result);
        }
        vcol_ = vcoh_ = vce_ = 0;
        break;
    }
    case 0x25:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const s16 left = vec_s16(vs, lane);
            const s16 right = selected_s16(vt, element, lane);
            u16 result = 0;
            const bool opposite = (left < 0) != (right < 0);
            set_flag(vcol_, lane, opposite);
            if (opposite) {
                const s32 sum = static_cast<s32>(left) + right;
                set_flag(vccl_, lane, sum <= 0);
                set_flag(vcch_, lane, right < 0);
                set_flag(vcoh_, lane,
                         sum != 0 && static_cast<u16>(left) != static_cast<u16>(~static_cast<u16>(right)));
                set_flag(vce_, lane, sum == -1);
                result = flag(vccl_, lane) ? static_cast<u16>(0u - static_cast<u16>(right))
                                           : static_cast<u16>(left);
            } else {
                const s32 difference = static_cast<s32>(left) - right;
                set_flag(vccl_, lane, right < 0);
                set_flag(vcch_, lane, difference >= 0);
                set_flag(vcoh_, lane,
                         difference != 0 &&
                             static_cast<u16>(left) != static_cast<u16>(~static_cast<u16>(right)));
                set_flag(vce_, lane, false);
                result = flag(vcch_, lane) ? static_cast<u16>(right) : static_cast<u16>(left);
            }
            set_acc_low(lane, result);
            vec_set_u16(vd, lane, result);
        }
        break;
    case 0x26:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const s16 left = vec_s16(vs, lane);
            const s16 right = selected_s16(vt, element, lane);
            const bool opposite = (left < 0) != (right < 0);
            u16 result = 0;
            if (opposite) {
                set_flag(vcch_, lane, right < 0);
                set_flag(vccl_, lane, static_cast<s32>(left) + right + 1 <= 0);
                result =
                    flag(vccl_, lane) ? static_cast<u16>(~static_cast<u16>(right)) : static_cast<u16>(left);
            } else {
                set_flag(vccl_, lane, right < 0);
                set_flag(vcch_, lane, static_cast<s32>(left) - right >= 0);
                result = flag(vcch_, lane) ? static_cast<u16>(right) : static_cast<u16>(left);
            }
            set_acc_low(lane, result);
            vec_set_u16(vd, lane, result);
        }
        vcol_ = vcoh_ = vce_ = 0;
        break;
    case 0x27:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const u16 result = flag(vccl_, lane) ? vec_u16(vs, lane) : selected_u16(vt, element, lane);
            set_acc_low(lane, result);
            vec_set_u16(vd, lane, result);
        }
        vcol_ = vcoh_ = 0;
        break;
    case 0x28:
    case 0x29:
    case 0x2a:
    case 0x2b:
    case 0x2c:
    case 0x2d:
        for (unsigned lane = 0; lane < 8; ++lane) {
            const u16 left = vec_u16(vs, lane);
            const u16 right = selected_u16(vt, element, lane);
            u16 result = 0;
            switch (function) {
            case 0x28:
                result = left & right;
                break;
            case 0x29:
                result = static_cast<u16>(~(left & right));
                break;
            case 0x2a:
                result = left | right;
                break;
            case 0x2b:
                result = static_cast<u16>(~(left | right));
                break;
            case 0x2c:
                result = left ^ right;
                break;
            default:
                result = static_cast<u16>(~(left ^ right));
                break;
            }
            set_acc_low(lane, result);
            vec_set_u16(vd, lane, result);
        }
        break;
    case 0x30:
    case 0x31:
    case 0x34:
    case 0x35: {
        const bool square_root = function == 0x34 || function == 0x35;
        const bool use_high = function == 0x31 || function == 0x35;
        const u16 low = vec_u16(vt, element & 7);
        u32 input = static_cast<u32>(static_cast<s32>(std::bit_cast<s16>(low)));
        if (use_high && div_input_high_) {
            input = (static_cast<u32>(static_cast<u16>(div_input_)) << 16) | low;
        }
        const u32 result = square_root ? reciprocal_sqrt(input) : reciprocal(input);
        div_input_high_ = false;
        div_output_ = std::bit_cast<s16>(static_cast<u16>(result >> 16));
        copy_selected_to_acc_low();
        vec_set_u16(vd, de, static_cast<u16>(result));
        break;
    }
    case 0x32:
    case 0x36:
        copy_selected_to_acc_low();
        div_input_high_ = true;
        div_input_ = std::bit_cast<s16>(vec_u16(vt, element & 7));
        vec_set_u16(vd, de, std::bit_cast<u16>(div_output_));
        break;
    case 0x33:
        copy_selected_to_acc_low();
        vec_set_u16(vd, de, selected_u16(vt, element, de));
        break;
    case 0x37:
    case 0x3f:
        break;
    default:
        break;
    }
}

} // namespace cupid
