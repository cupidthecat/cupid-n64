#include "cupid/rsp.hpp"

#include "cupid/system.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace cupid {

namespace {

constexpr u32 mask_pc(u32 value) {
    return value & 0x0ffc;
}

constexpr s32 as_s32(u32 value) {
    return std::bit_cast<s32>(value);
}

constexpr u32 as_u32(s32 value) {
    return std::bit_cast<u32>(value);
}

constexpr s16 immediate16(u32 instruction) {
    return std::bit_cast<s16>(static_cast<u16>(instruction));
}

} // namespace

Rsp::Rsp(System& system) : system_(system) {
    reset();
}

void Rsp::reset() {
    memory.fill(0);
    gpr_.fill(0);
    vr_ = {};
    accumulator_.fill(0);
    vcol_ = vcoh_ = vccl_ = vcch_ = vce_ = 0;
    div_input_ = 0;
    div_output_ = 0;
    div_input_high_ = false;

    halted_ = true;
    broke_ = false;
    single_step_ = false;
    interrupt_on_break_ = false;
    signal_.fill(false);
    semaphore_ = false;

    dma_current_ = {};
    dma_current_.length = 0x0ff8;
    dma_pending_ = {};
    dma_busy_ = false;
    dma_full_ = false;
    dma_cycles_until_row_ = 0;
    pc = 0;
    next_pc_ = 4;
    pc_shadow_ = 0;
    current_pc_ = 0;
    branch_pending_ = false;
    pipeline_.reset();
}

void Rsp::tick(u64 rcp_cycles) {
    while (rcp_cycles != 0 && !halted_) {
        tick_dma(1);
        step();
        --rcp_cycles;
    }
    tick_dma(rcp_cycles);
}

u64 Rsp::next_dma_event() const {
    return dma_busy_ ? dma_cycles_until_row_ : std::numeric_limits<u64>::max();
}

u8 Rsp::dmem_read8(u32 address) const {
    return memory[address & 0x0fff];
}

u16 Rsp::dmem_read16(u32 address) const {
    return static_cast<u16>((static_cast<u16>(dmem_read8(address)) << 8) | dmem_read8(address + 1));
}

u32 Rsp::dmem_read32(u32 address) const {
    return (static_cast<u32>(dmem_read8(address)) << 24) | (static_cast<u32>(dmem_read8(address + 1)) << 16) |
           (static_cast<u32>(dmem_read8(address + 2)) << 8) | static_cast<u32>(dmem_read8(address + 3));
}

void Rsp::dmem_write8(u32 address, u8 value) {
    memory[address & 0x0fff] = value;
}

void Rsp::dmem_write16(u32 address, u16 value) {
    dmem_write8(address, static_cast<u8>(value >> 8));
    dmem_write8(address + 1, static_cast<u8>(value));
}

void Rsp::dmem_write32(u32 address, u32 value) {
    dmem_write8(address, static_cast<u8>(value >> 24));
    dmem_write8(address + 1, static_cast<u8>(value >> 16));
    dmem_write8(address + 2, static_cast<u8>(value >> 8));
    dmem_write8(address + 3, static_cast<u8>(value));
}

u32 Rsp::fetch_instruction(u32 address) const {
    return read_be32(&memory[0x1000U | mask_pc(address)]);
}

void Rsp::write_gpr(unsigned index, u32 value) {
    if (index != 0) {
        gpr_[index & 31] = value;
    }
}

void Rsp::take_branch(u32 target) {
    next_pc_ = mask_pc(target);
    branch_pending_ = true;
}

void Rsp::write_pc(u32 value) {
    pc = mask_pc(value);
    next_pc_ = mask_pc(pc + 4);
    pc_shadow_ = pc;
    branch_pending_ = false;
    pipeline_.redirect();
}

void Rsp::step() {
    if (halted_) {
        return;
    }

    // Keep direct host changes synchronized. Register writes use write_pc()
    // because even an unchanged address must discard a pending branch.
    if (pc != pc_shadow_)
        write_pc(pc);

    if (pipeline_.advance_branch_wait())
        return;
    pipeline_.fetch(fetch_instruction(pc), fetch_instruction(pc + 4), single_step_);
    if (pipeline_.advance_operand_wait())
        return;

    bool taken_delay_slot = false;
    const unsigned count = pipeline_.size();
    for (unsigned index = 0; index < count; ++index) {
        taken_delay_slot = taken_delay_slot || branch_pending_;
        branch_pending_ = false;
        current_pc_ = mask_pc(pc);
        pc = mask_pc(next_pc_);
        next_pc_ = mask_pc(pc + 4);
        execute_scalar(pipeline_.instruction(index));
        gpr_[0] = 0;
        pc_shadow_ = pc;
    }
    pipeline_.retire(taken_delay_slot, pc);

    if (single_step_ && !halted_) {
        halted_ = true;
    }
}

void Rsp::execute_scalar(u32 instruction) {
    const unsigned op = instruction >> 26;
    const unsigned rs = (instruction >> 21) & 31;
    const unsigned rt = (instruction >> 16) & 31;
    const s16 imm = immediate16(instruction);

    switch (op) {
    case 0x00:
        execute_special(instruction);
        break;
    case 0x01:
        execute_regimm(instruction);
        break;
    case 0x02:
        take_branch((instruction & 0x03ff'ffff) << 2);
        break;
    case 0x03:
        write_gpr(31, mask_pc(current_pc_ + 8));
        take_branch((instruction & 0x03ff'ffff) << 2);
        break;
    case 0x04:
        if (gpr_[rs] == gpr_[rt]) {
            take_branch(current_pc_ + 4 + static_cast<u32>(static_cast<s32>(imm) * 4));
        }
        break;
    case 0x05:
        if (gpr_[rs] != gpr_[rt]) {
            take_branch(current_pc_ + 4 + static_cast<u32>(static_cast<s32>(imm) * 4));
        }
        break;
    case 0x06:
        if (as_s32(gpr_[rs]) <= 0) {
            take_branch(current_pc_ + 4 + static_cast<u32>(static_cast<s32>(imm) * 4));
        }
        break;
    case 0x07:
        if (as_s32(gpr_[rs]) > 0) {
            take_branch(current_pc_ + 4 + static_cast<u32>(static_cast<s32>(imm) * 4));
        }
        break;
    case 0x08:
    case 0x09:
        write_gpr(rt, gpr_[rs] + static_cast<u32>(static_cast<s32>(imm)));
        break;
    case 0x0a:
        write_gpr(rt, as_s32(gpr_[rs]) < static_cast<s32>(imm));
        break;
    case 0x0b:
        write_gpr(rt, gpr_[rs] < static_cast<u32>(static_cast<s32>(imm)));
        break;
    case 0x0c:
        write_gpr(rt, gpr_[rs] & static_cast<u16>(instruction));
        break;
    case 0x0d:
        write_gpr(rt, gpr_[rs] | static_cast<u16>(instruction));
        break;
    case 0x0e:
        write_gpr(rt, gpr_[rs] ^ static_cast<u16>(instruction));
        break;
    case 0x0f:
        write_gpr(rt, static_cast<u32>(static_cast<u16>(instruction)) << 16);
        break;
    case 0x10:
        execute_cop0(instruction);
        break;
    case 0x12:
        execute_cop2(instruction);
        break;
    case 0x20:
        write_gpr(rt, as_u32(static_cast<s32>(
                          static_cast<s8>(dmem_read8(gpr_[rs] + static_cast<u32>(static_cast<s32>(imm)))))));
        break;
    case 0x21:
        write_gpr(rt, as_u32(static_cast<s32>(static_cast<s16>(
                          dmem_read16(gpr_[rs] + static_cast<u32>(static_cast<s32>(imm)))))));
        break;
    case 0x23:
    case 0x27:
        write_gpr(rt, dmem_read32(gpr_[rs] + static_cast<u32>(static_cast<s32>(imm))));
        break;
    case 0x24:
        write_gpr(rt, dmem_read8(gpr_[rs] + static_cast<u32>(static_cast<s32>(imm))));
        break;
    case 0x25:
        write_gpr(rt, dmem_read16(gpr_[rs] + static_cast<u32>(static_cast<s32>(imm))));
        break;
    case 0x28:
        dmem_write8(gpr_[rs] + static_cast<u32>(static_cast<s32>(imm)), static_cast<u8>(gpr_[rt]));
        break;
    case 0x29:
        dmem_write16(gpr_[rs] + static_cast<u32>(static_cast<s32>(imm)), static_cast<u16>(gpr_[rt]));
        break;
    case 0x2b:
        dmem_write32(gpr_[rs] + static_cast<u32>(static_cast<s32>(imm)), gpr_[rt]);
        break;
    case 0x32:
        execute_vector_load(instruction);
        break;
    case 0x3a:
        execute_vector_store(instruction);
        break;
    default:
        break;
    }
}

void Rsp::execute_special(u32 instruction) {
    const unsigned rs = (instruction >> 21) & 31;
    const unsigned rt = (instruction >> 16) & 31;
    const unsigned rd = (instruction >> 11) & 31;
    const unsigned sa = (instruction >> 6) & 31;
    const unsigned function = instruction & 63;

    switch (function) {
    case 0x00:
        write_gpr(rd, gpr_[rt] << sa);
        break;
    case 0x02:
        write_gpr(rd, gpr_[rt] >> sa);
        break;
    case 0x03:
        write_gpr(rd, as_u32(as_s32(gpr_[rt]) >> sa));
        break;
    case 0x04:
        write_gpr(rd, gpr_[rt] << (gpr_[rs] & 31));
        break;
    case 0x06:
        write_gpr(rd, gpr_[rt] >> (gpr_[rs] & 31));
        break;
    case 0x07:
        write_gpr(rd, as_u32(as_s32(gpr_[rt]) >> (gpr_[rs] & 31)));
        break;
    case 0x08:
        take_branch(gpr_[rs]);
        break;
    case 0x09: {
        const u32 target = gpr_[rs];
        write_gpr(rd, mask_pc(current_pc_ + 8));
        take_branch(target);
        break;
    }
    case 0x0d:
        halted_ = true;
        broke_ = true;
        if (interrupt_on_break_) {
            system_.bus.set_interrupt(0, true);
        }
        break;
    case 0x20:
    case 0x21:
        write_gpr(rd, gpr_[rs] + gpr_[rt]);
        break;
    case 0x22:
    case 0x23:
        write_gpr(rd, gpr_[rs] - gpr_[rt]);
        break;
    case 0x24:
        write_gpr(rd, gpr_[rs] & gpr_[rt]);
        break;
    case 0x25:
        write_gpr(rd, gpr_[rs] | gpr_[rt]);
        break;
    case 0x26:
        write_gpr(rd, gpr_[rs] ^ gpr_[rt]);
        break;
    case 0x27:
        write_gpr(rd, ~(gpr_[rs] | gpr_[rt]));
        break;
    case 0x2a:
        write_gpr(rd, as_s32(gpr_[rs]) < as_s32(gpr_[rt]));
        break;
    case 0x2b:
        write_gpr(rd, gpr_[rs] < gpr_[rt]);
        break;
    default:
        // Undefined SPECIAL encodings have observable shift behavior on the
        // signal processor instead of raising a CPU-style exception.
        write_gpr(rd, gpr_[rs] >> (gpr_[rs] & 31));
        break;
    }
}

void Rsp::execute_regimm(u32 instruction) {
    const unsigned rs = (instruction >> 21) & 31;
    const unsigned kind = (instruction >> 16) & 31;
    const s16 imm = immediate16(instruction);
    const bool negative = as_s32(gpr_[rs]) < 0;
    const u32 target = current_pc_ + 4 + static_cast<u32>(static_cast<s32>(imm) * 4);

    switch (kind) {
    case 0x00:
        if (negative)
            take_branch(target);
        break;
    case 0x01:
        if (!negative)
            take_branch(target);
        break;
    case 0x10:
        write_gpr(31, mask_pc(current_pc_ + 8));
        if (negative)
            take_branch(target);
        break;
    case 0x11:
        write_gpr(31, mask_pc(current_pc_ + 8));
        if (!negative)
            take_branch(target);
        break;
    default:
        break;
    }
}

void Rsp::execute_cop0(u32 instruction) {
    const unsigned rs = (instruction >> 21) & 31;
    const unsigned rt = (instruction >> 16) & 31;
    const unsigned rd = (instruction >> 11) & 31;

    if ((rd & 8U) != 0) {
        if (rs == 0) {
            write_gpr(rt, system_.bus.rdp.read_register((rd & 7) << 2));
        } else if (rs == 4) {
            system_.bus.rdp.write_register((rd & 7) << 2, gpr_[rt]);
        }
        return;
    }

    if (rs == 0) {
        write_gpr(rt, read_register(rd << 2));
    } else if (rs == 4) {
        write_register(rd << 2, gpr_[rt]);
    }
}

u32 Rsp::read_register(u32 byte_offset) {
    switch (byte_offset & 0x1c) {
    case 0x00:
        return dma_current_.sp_address & 0x1fff;
    case 0x04:
        return dma_current_.dram_address & 0x00ff'ffff;
    case 0x08:
    case 0x0c:
        return static_cast<u32>(dma_current_.length) | (static_cast<u32>(dma_current_.count) << 12) |
               (static_cast<u32>(dma_current_.skip) << 20);
    case 0x10: {
        u32 value = 0;
        value |= halted_ ? 1u << 0 : 0;
        value |= broke_ ? 1u << 1 : 0;
        value |= dma_busy_ ? 1u << 2 : 0;
        value |= dma_full_ ? 1u << 3 : 0;
        value |= single_step_ ? 1u << 5 : 0;
        value |= interrupt_on_break_ ? 1u << 6 : 0;
        for (unsigned i = 0; i < signal_.size(); ++i) {
            value |= signal_[i] ? 1u << (7 + i) : 0;
        }
        return value;
    }
    case 0x14:
        return dma_full_;
    case 0x18:
        return dma_busy_;
    case 0x1c: {
        const u32 value = semaphore_ ? 1 : 0;
        semaphore_ = true;
        return value;
    }
    default:
        return 0;
    }
}

void Rsp::write_register(u32 byte_offset, u32 value) {
    switch (byte_offset & 0x1c) {
    case 0x00:
        dma_pending_.sp_address = static_cast<u16>(value & 0x1ff8);
        break;
    case 0x04:
        dma_pending_.dram_address = value & 0x00ff'fff8;
        break;
    case 0x08:
        start_dma(true, value);
        break;
    case 0x0c:
        start_dma(false, value);
        break;
    case 0x10: {
        const auto paired = [value](unsigned clear_bit, unsigned set_bit, bool& field) {
            const bool clear = (value >> clear_bit) & 1;
            const bool set = (value >> set_bit) & 1;
            if (clear != set) {
                field = set;
            }
        };

        paired(0, 1, halted_);
        if (value & (1u << 2))
            broke_ = false;

        const bool clear_irq = value & (1u << 3);
        const bool set_irq = value & (1u << 4);
        if (clear_irq != set_irq) {
            system_.bus.set_interrupt(0, set_irq);
        }

        paired(5, 6, single_step_);
        paired(7, 8, interrupt_on_break_);
        for (unsigned i = 0; i < signal_.size(); ++i) {
            paired(9 + i * 2, 10 + i * 2, signal_[i]);
        }
        break;
    }
    case 0x14:
    case 0x18:
        break;
    case 0x1c:
        semaphore_ = false;
        break;
    }
}

void Rsp::start_dma(bool to_sp, u32 value) {
    dma_pending_.length = static_cast<u16>(value & 0x0ff8);
    dma_pending_.count = static_cast<u8>((value >> 12) & 0xff);
    dma_pending_.skip = static_cast<u16>((value >> 20) & 0x0ff8);
    dma_pending_.to_sp = to_sp;
    dma_full_ = true;
    promote_dma();
}

void Rsp::promote_dma() {
    if (dma_busy_ || !dma_full_) {
        return;
    }

    dma_current_ = dma_pending_;
    dma_busy_ = true;
    dma_full_ = false;
    dma_cycles_until_row_ = (static_cast<u64>(dma_current_.length) + 8) / 8;
}

void Rsp::tick_dma(u64 rcp_cycles) {
    while (dma_busy_ && rcp_cycles != 0) {
        if (dma_cycles_until_row_ > rcp_cycles) {
            dma_cycles_until_row_ -= rcp_cycles;
            return;
        }

        rcp_cycles -= dma_cycles_until_row_;
        dma_cycles_until_row_ = 0;
        transfer_dma_row();
    }
}

void Rsp::transfer_dma_row() {
    const u32 bank = dma_current_.sp_address & 0x1000;
    u32 sp_offset = dma_current_.sp_address & 0x0fff;
    u32 dram = dma_current_.dram_address & 0x00ff'ffff;
    const u32 bytes = static_cast<u32>(dma_current_.length) + 8;

    for (u32 i = 0; i < bytes; i += 8) {
        const u32 sp_index = bank | (sp_offset & 0x0fff);
        if (dma_current_.to_sp) {
            if (bank != 0) {
                const u64 value = system_.bus.memory.read(dram, 8);
                for (unsigned byte = 0; byte < 8; ++byte) {
                    memory[sp_index + byte] = static_cast<u8>(value >> ((7u - byte) * 8u));
                }
            } else {
                const u32 high = static_cast<u32>(system_.bus.memory.read(dram, 4));
                const u32 low = static_cast<u32>(system_.bus.memory.read(dram + 4, 4));
                write_be32(&memory[sp_index], high);
                write_be32(&memory[sp_index + 4], low);
            }
        } else {
            if (bank != 0) {
                u64 value = 0;
                for (unsigned byte = 0; byte < 8; ++byte) {
                    value = (value << 8) | memory[sp_index + byte];
                }
                system_.bus.memory.write(dram, 8, value);
            } else {
                system_.bus.memory.write(dram, 4, read_be32(&memory[sp_index]));
                system_.bus.memory.write(dram + 4, 4, read_be32(&memory[sp_index + 4]));
            }
        }
        sp_offset = (sp_offset + 8) & 0x0fff;
        dram = (dram + 8) & 0x00ff'ffff;
    }

    dma_current_.sp_address = static_cast<u16>(bank | sp_offset);
    dma_current_.dram_address = dram;

    if (dma_current_.count != 0) {
        --dma_current_.count;
        dma_current_.dram_address = (dma_current_.dram_address + dma_current_.skip) & 0x00ff'ffff;
        dma_cycles_until_row_ = (static_cast<u64>(dma_current_.length) + 8) / 8;
        return;
    }

    dma_current_.length = 0x0ff8;
    dma_busy_ = false;
    promote_dma();
}

} // namespace cupid
