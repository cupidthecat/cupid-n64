#include "cupid/system.hpp"

namespace cupid {

u64 Cpu::read_cop0(unsigned index) {
    index &= 31U;
    if (index == 1)
        return random_;
    switch (index) {
    case 7:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 31:
        return cop0_latch_;
    default:
        return cp0[index];
    }
}

void Cpu::write_cop0(unsigned index, u64 value) {
    index &= 31U;
    cop0_latch_ = value;
    switch (index) {
    case 0:
        cp0[0] = value & 0x8000003fU;
        return;
    case 1:
    case 8:
    case 15:
    case 29:
        return;
    case 2:
    case 3:
        cp0[index] = value & 0x3fffffffU;
        return;
    case 4:
        cp0[4] = (value & ~0x7fffffULL) | (cp0[4] & 0x7ffff0U);
        return;
    case 5:
        cp0[5] = value & 0x01ffe000U;
        return;
    case 6:
        cp0[6] = value & 63U;
        random_ = 31;
        random_wired_ = static_cast<u32>(cp0[6]);
        wired_writes_.fill({});
        return;
    case 9:
        cp0[9] = static_cast<u32>(value);
        count_half_ = false;
        count_write_hold_ = 0;
        return;
    case 10:
        cp0[10] = value & 0xc00000ffffffe0ffULL;
        return;
    case 11:
        cp0[11] = static_cast<u32>(value);
        cp0[13] &= ~0x8000ULL;
        return;
    case 12:
        cp0[12] = (value & 0xff57ffffU) | (cp0[12] & 0x00200000U);
        return;
    case 13:
        cp0[13] = (cp0[13] & ~0x300ULL) | (value & 0x300U);
        return;
    case 14:
    case 30:
        cp0[index] = value;
        return;
    case 16:
        cp0[16] = (cp0[16] & ~0x0f00800fULL) | (value & 0x0f00800fU);
        return;
    case 17:
    case 28:
        cp0[index] = static_cast<u32>(value);
        return;
    case 18:
        cp0[18] = value & 0xfffffffbU;
        return;
    case 19:
        cp0[19] = value & 15U;
        return;
    case 20:
        cp0[20] = (value & ~0x1ffffffffULL) | (cp0[20] & 0x1fffffff0ULL);
        return;
    case 26:
        cp0[26] = value & 255U;
        return;
    case 27:
        cp0[27] = 0;
        return;
    default:
        return;
    }
}

void Cpu::write_cop0_instruction(unsigned index, u64 value) {
    add_cycles(1);
    if (index == 6) {
        cop0_latch_ = value;
        cp0[6] = value & 63U;
        wired_writes_[instruction_count & 1U] = {static_cast<u32>(cp0[6]), instruction_count + 2};
        return;
    }
    write_cop0(index, value);
    if (index == 9)
        count_write_hold_ = instruction_cycles_ - synchronized_instruction_cycles_ + 1;
    if (index == 13)
        software_interrupt_delay_ = 1;
}

void Cpu::tlb_write(unsigned index) {
    if (index >= tlb.size())
        return;
    auto& entry = tlb[index];
    u32 mask = static_cast<u32>(cp0[5]) & 0x01554000U;
    mask |= mask >> 1;
    entry.page_mask = mask;
    entry.entry_hi = cp0[10] & ~static_cast<u64>(mask);
    entry.global = (cp0[2] & cp0[3] & 1U) != 0;
    for (unsigned page = 0; page < 2; ++page) {
        entry.entry_lo[page] =
            (static_cast<u32>(cp0[page + 2]) & 0x03fffffeU) | static_cast<u32>(entry.global);
    }
}

void Cpu::tlb_read() {
    const unsigned index = static_cast<unsigned>(cp0[0] & 63U);
    if (index >= tlb.size())
        return;
    const auto& entry = tlb[index];
    cp0[5] = entry.page_mask;
    cp0[10] = entry.entry_hi;
    cp0[2] = entry.entry_lo[0];
    cp0[3] = entry.entry_lo[1];
}

void Cpu::tlb_probe() {
    cp0[0] = 0x80000000U;
    for (unsigned index = 0; index < tlb.size(); ++index) {
        const auto& entry = tlb[index];
        const u64 mask = 0xc00000ffffffe000ULL & ~static_cast<u64>(entry.page_mask);
        if ((cp0[10] & mask) != (entry.entry_hi & mask))
            continue;
        if (!entry.global && (entry.entry_hi & 255U) != (cp0[10] & 255U))
            continue;
        cp0[0] = index;
        return;
    }
}

void Cpu::execute_cop0(u32 instruction) {
    const unsigned function = (instruction >> 21) & 31U;
    const unsigned rt = (instruction >> 16) & 31U;
    const unsigned rd = (instruction >> 11) & 31U;
    if (function == 2 || function == 6 || function == 8)
        return;
    if (function < 16) {
        switch (function) {
        case 0:
            if (require_coprocessor(0))
                gpr[rt] = sign_extend32(static_cast<u32>(read_cop0(rd)));
            return;
        case 1:
            if (!require_coprocessor(0))
                return;
            if (!wide_instructions())
                raise_exception(Exception::ReservedInstruction);
            else
                gpr[rt] = read_cop0(rd);
            return;
        case 4:
            if (require_coprocessor(0)) {
                write_cop0_instruction(rd, gpr[rt]);
            }
            return;
        case 5:
            if (!require_coprocessor(0))
                return;
            if (!wide_instructions())
                raise_exception(Exception::ReservedInstruction);
            else {
                write_cop0_instruction(rd, gpr[rt]);
            }
            return;
        default:
            raise_exception(Exception::ReservedInstruction);
            return;
        }
    }

    switch (instruction & 63U) {
    case 0x01:
        if (require_coprocessor(0))
            tlb_read();
        return;
    case 0x02:
        if (require_coprocessor(0))
            tlb_write(static_cast<unsigned>(cp0[0] & 63U));
        return;
    case 0x06:
        if (require_coprocessor(0))
            tlb_write(random_);
        return;
    case 0x08:
        if (require_coprocessor(0))
            tlb_probe();
        return;
    case 0x10:
        raise_exception(Exception::ReservedInstruction);
        return;
    case 0x18:
        if (!require_coprocessor(0))
            return;
        add_cycles(1);
        if ((status() & 4U) != 0) {
            set_pc(cp0[30]);
            cp0[12] &= ~4ULL;
        } else {
            set_pc(cp0[14]);
            cp0[12] &= ~2ULL;
        }
        linked = false;
        redirected_ = true;
        return;
    default:
        return;
    }
}

} // namespace cupid
