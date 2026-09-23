#include "cupid/rsp.hpp"

#include "cupid/system.hpp"

#include <algorithm>
#include <limits>

namespace cupid {

namespace {

constexpr u32 mask_pc(u32 value) {
    return value & 0x0ffc;
}

} // namespace

Rsp::Rsp(System& system) : system_(system) {
    reset();
}

void Rsp::reset() {
    memory.fill(0);
    gpr_.fill(0);
    vr_ = {};
    accumulator_ = {};
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
    while (rcp_cycles != 0) {
        if (halted_) {
            if (!pipeline_.advance_branch_wait())
                break;
            tick_dma(1);
        } else {
            tick_dma(1);
            step();
        }
        --rcp_cycles;
    }
    tick_dma(rcp_cycles);
}

u64 Rsp::next_dma_event() const {
    return dma_busy_ ? dma_cycles_until_row_ : std::numeric_limits<u64>::max();
}

bool Rsp::local_execution_ready() const {
    return !halted_ && !single_step_ && pc == pc_shadow_;
}

bool Rsp::step_local() {
    RspPipeline::LocalIssue issue = RspPipeline::LocalIssue::Blocked;
    if (pipeline_.size() == 0U) {
        const std::array<u32, 2> words{fetch_instruction(pc), fetch_instruction(pc + 4)};
        issue = pipeline_.local_issue(words[0], words[1], pc);
    } else {
        issue = pipeline_.local_issue();
    }

    if (issue == RspPipeline::LocalIssue::Blocked)
        return false;
    if (issue == RspPipeline::LocalIssue::Ready)
        execute_group();
    return true;
}

u64 Rsp::run_local(u64 maximum_cycles) {
    if (!local_execution_ready())
        return 0;

    if (dma_busy_) {
        // The row's payload stays unchanged before its transfer cycle. Leave that
        // cycle to tick(), which transfers the row before issuing its instruction.
        if (dma_cycles_until_row_ <= 1)
            return 0;
        maximum_cycles = std::min(maximum_cycles, dma_cycles_until_row_ - 1);
    }

    if (maximum_cycles < 8) {
        u64 elapsed = 0;
        while (elapsed < maximum_cycles && step_local())
            ++elapsed;
        tick_dma(elapsed);
        return elapsed;
    }

    RspPipeline::LocalWindow window;
    const std::span<const u8, 4096> imem(memory.data() + 0x1000U, 4096U);
    const auto step_window = [&] {
        RspPipeline::LocalIssue issue = RspPipeline::LocalIssue::Blocked;
        if (pipeline_.size() == 0U)
            issue = pipeline_.local_issue(imem, window, pc);
        else
            issue = pipeline_.local_issue();

        if (issue == RspPipeline::LocalIssue::Blocked)
            return false;
        if (issue == RspPipeline::LocalIssue::Ready)
            execute_group();
        return true;
    };

    u64 elapsed = 0;
    while (elapsed < maximum_cycles && step_window()) {
        // A branch-wait or operand-wait cycle is still an RSP cycle. Local groups
        // cannot change shared registers, start DMA, halt, or raise an SP interrupt.
        ++elapsed;
    }
    tick_dma(elapsed);
    return elapsed;
}

u8 Rsp::dmem_read8(u32 address) const {
    return memory[address & 0x0fff];
}

u16 Rsp::dmem_read16(u32 address) const {
    const u32 offset = address & 0x0fffU;
    if (offset + 2U <= 0x1000U)
        return read_be16(memory.data() + offset);
    return static_cast<u16>((static_cast<u16>(dmem_read8(address)) << 8) | dmem_read8(address + 1));
}

u32 Rsp::dmem_read32(u32 address) const {
    const u32 offset = address & 0x0fffU;
    if (offset + 4U <= 0x1000U)
        return read_be32(memory.data() + offset);
    return (static_cast<u32>(dmem_read8(address)) << 24) | (static_cast<u32>(dmem_read8(address + 1)) << 16) |
           (static_cast<u32>(dmem_read8(address + 2)) << 8) | static_cast<u32>(dmem_read8(address + 3));
}

void Rsp::dmem_write8(u32 address, u8 value) {
    memory[address & 0x0fff] = value;
}

void Rsp::dmem_write16(u32 address, u16 value) {
    const u32 offset = address & 0x0fffU;
    if (offset + 2U <= 0x1000U) {
        write_be16(memory.data() + offset, value);
        return;
    }
    dmem_write8(address, static_cast<u8>(value >> 8));
    dmem_write8(address + 1, static_cast<u8>(value));
}

void Rsp::dmem_write32(u32 address, u32 value) {
    const u32 offset = address & 0x0fffU;
    if (offset + 4U <= 0x1000U) {
        write_be32(memory.data() + offset, value);
        return;
    }
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
    if (pipeline_.size() == 0U)
        pipeline_.fetch(fetch_instruction(pc), fetch_instruction(pc + 4), single_step_, pc);
    if (pipeline_.advance_operand_wait())
        return;

    execute_group();

    if (single_step_ && !halted_) {
        halted_ = true;
    }
}

void Rsp::execute_group() {
    bool taken_delay_slot = false;
    const unsigned count = pipeline_.size();
    for (unsigned index = 0; index < count; ++index) {
        taken_delay_slot = taken_delay_slot || branch_pending_;
        branch_pending_ = false;
        current_pc_ = mask_pc(pc);
        pc = mask_pc(next_pc_);
        next_pc_ = mask_pc(pc + 4);
        execute_decoded(pipeline_.instruction(index), pipeline_.operation(index));
        gpr_[0] = 0;
        pc_shadow_ = pc;
    }
    pipeline_.retire(taken_delay_slot, pc);
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
    system_.settle();
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
    system_.settle();
    // A DMA or a running RSP changes how long the devices may lag behind the CPU.
    system_.defer_limit_ = 0;
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
