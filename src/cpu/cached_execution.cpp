#include "cupid/system.hpp"

#include <algorithm>
#include <bitset>
#include <cassert>
#include <limits>

namespace cupid {

Cpu::CachedDecode Cpu::decode_cached_instruction(u32 instruction) const {
    CachedDecode decoded{};
    decoded.word = instruction;
    decoded.valid = true;
    decoded.load_target = 32;

    const unsigned op = instruction >> 26;
    const unsigned rs = (instruction >> 21) & 31U;
    const unsigned rt = (instruction >> 16) & 31U;
    decoded.rs = static_cast<u8>(rs);
    decoded.rt = static_cast<u8>(rt);
    decoded.rd = static_cast<u8>((instruction >> 11) & 31U);
    decoded.sa = static_cast<u8>((instruction >> 6) & 31U);
    bool check_rs = op != 2 && op != 3;
    bool check_rt = check_rs;
    if (op == 0x11 || op == 0x12) {
        check_rs = false;
        check_rt = rs < 8;
    } else if (op == 0x31 || op == 0x35 || op == 0x39 || op == 0x3d) {
        check_rt = false;
    }
    decoded.integer_reads = (check_rs ? 1U << rs : 0U) | (check_rt ? 1U << rt : 0U);
    const auto direct = [&](CachedDirect operation) {
        decoded.kind = CachedKind::Private;
        decoded.direct = operation;
    };

    if (op == 0) {
        switch (instruction & 63U) {
        case 0x00:
            direct(CachedDirect::Sll);
            break;
        case 0x02:
            direct(CachedDirect::Srl);
            break;
        case 0x03:
            direct(CachedDirect::Sra);
            break;
        case 0x04:
            direct(CachedDirect::Sllv);
            break;
        case 0x06:
            direct(CachedDirect::Srlv);
            break;
        case 0x07:
            direct(CachedDirect::Srav);
            break;
        case 0x08:
            direct(CachedDirect::Jr);
            break;
        case 0x09:
            direct(CachedDirect::Jalr);
            break;
        case 0x0f:
            direct(CachedDirect::Sync);
            break;
        case 0x10:
            direct(CachedDirect::Mfhi);
            break;
        case 0x11:
            direct(CachedDirect::Mthi);
            break;
        case 0x12:
            direct(CachedDirect::Mflo);
            break;
        case 0x13:
            direct(CachedDirect::Mtlo);
            break;
        case 0x14:
            direct(CachedDirect::Dsllv);
            break;
        case 0x16:
            direct(CachedDirect::Dsrlv);
            break;
        case 0x17:
            direct(CachedDirect::Dsrav);
            break;
        case 0x21:
            direct(CachedDirect::Addu);
            break;
        case 0x23:
            direct(CachedDirect::Subu);
            break;
        case 0x24:
            direct(CachedDirect::And);
            break;
        case 0x25:
            direct(CachedDirect::Or);
            break;
        case 0x26:
            direct(CachedDirect::Xor);
            break;
        case 0x27:
            direct(CachedDirect::Nor);
            break;
        case 0x2a:
            direct(CachedDirect::Slt);
            break;
        case 0x2b:
            direct(CachedDirect::Sltu);
            break;
        case 0x2d:
            direct(CachedDirect::Daddu);
            break;
        case 0x2f:
            direct(CachedDirect::Dsubu);
            break;
        case 0x38:
            direct(CachedDirect::Dsll);
            break;
        case 0x3a:
            direct(CachedDirect::Dsrl);
            break;
        case 0x3b:
            direct(CachedDirect::Dsra);
            break;
        case 0x3c:
            direct(CachedDirect::Dsll32);
            break;
        case 0x3e:
            direct(CachedDirect::Dsrl32);
            break;
        case 0x3f:
            direct(CachedDirect::Dsra32);
            break;
        default:
            break;
        }
        return decoded;
    }

    if (op == 1) {
        switch (rt) {
        case 0x00:
            direct(CachedDirect::Bltz);
            break;
        case 0x01:
            direct(CachedDirect::Bgez);
            break;
        case 0x10:
            direct(CachedDirect::Bltzal);
            break;
        case 0x11:
            direct(CachedDirect::Bgezal);
            break;
        default:
            break;
        }
        return decoded;
    }

    if (op == 0x11) {
        switch (rs) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x04:
        case 0x05:
            direct(CachedDirect::None);
            break;
        case 0x10:
        case 0x11:
            if ((instruction & 63U) <= 3U || (instruction & 63U) >= 0x30U)
                direct(CachedDirect::None);
            break;
        default:
            break;
        }
        return decoded;
    }

    switch (op) {
    case 0x02:
        direct(CachedDirect::J);
        break;
    case 0x03:
        direct(CachedDirect::Jal);
        break;
    case 0x04:
        direct(CachedDirect::Beq);
        break;
    case 0x05:
        direct(CachedDirect::Bne);
        break;
    case 0x06:
        direct(CachedDirect::Blez);
        break;
    case 0x07:
        direct(CachedDirect::Bgtz);
        break;
    case 0x09:
        direct(CachedDirect::Addiu);
        break;
    case 0x0a:
        direct(CachedDirect::Slti);
        break;
    case 0x0b:
        direct(CachedDirect::Sltiu);
        break;
    case 0x0c:
        direct(CachedDirect::Andi);
        break;
    case 0x0d:
        direct(CachedDirect::Ori);
        break;
    case 0x0e:
        direct(CachedDirect::Xori);
        break;
    case 0x0f:
        direct(CachedDirect::Lui);
        break;
    case 0x19:
        direct(CachedDirect::Daddiu);
        break;
    case 0x20:
    case 0x21:
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x27:
    case 0x37:
        decoded.kind = CachedKind::Load;
        decoded.load_target = static_cast<u8>(rt);
        break;
    case 0x31:
    case 0x35:
        decoded.kind = CachedKind::Load;
        decoded.floating_memory = true;
        break;
    case 0x28:
    case 0x29:
    case 0x2b:
    case 0x3f:
        decoded.kind = CachedKind::Store;
        break;
    case 0x39:
    case 0x3d:
        decoded.kind = CachedKind::Store;
        decoded.floating_memory = true;
        break;
    default:
        break;
    }
    if (decoded.kind == CachedKind::Load || decoded.kind == CachedKind::Store) {
        decoded.memory_width = op == 0x20 || op == 0x24 || op == 0x28                 ? 1U
                               : op == 0x21 || op == 0x25 || op == 0x29               ? 2U
                               : op == 0x35 || op == 0x37 || op == 0x3d || op == 0x3f ? 8U
                                                                                      : 4U;
        decoded.signed_load = decoded.kind == CachedKind::Load && op < 0x24;
    }
    return decoded;
}

void Cpu::execute_cached_direct(const CachedDecode& decoded) {
    const u64 a = gpr[decoded.rs];
    const u64 b = gpr[decoded.rt];
    const u64 immediate = sign_extend16(static_cast<u16>(decoded.word));
    const u64 target = next_pc + immediate * 4;
    const auto write = [&](unsigned index, u64 value) {
        if (index != 0)
            gpr[index] = value;
    };

    switch (decoded.direct) {
    case CachedDirect::Sll:
        write(decoded.rd, sign_extend32(static_cast<u32>(b) << decoded.sa));
        return;
    case CachedDirect::Srl:
        write(decoded.rd, sign_extend32(static_cast<u32>(b) >> decoded.sa));
        return;
    case CachedDirect::Sra:
        write(decoded.rd, sign_extend32(static_cast<u32>(signed64(b) >> decoded.sa)));
        return;
    case CachedDirect::Sllv:
        write(decoded.rd, sign_extend32(static_cast<u32>(b) << static_cast<unsigned>(a & 31U)));
        return;
    case CachedDirect::Srlv:
        write(decoded.rd, sign_extend32(static_cast<u32>(b) >> static_cast<unsigned>(a & 31U)));
        return;
    case CachedDirect::Srav:
        write(decoded.rd, sign_extend32(static_cast<u32>(signed64(b) >> static_cast<unsigned>(a & 31U))));
        return;
    case CachedDirect::Jr:
        branch(true, a);
        return;
    case CachedDirect::Jalr:
        write(decoded.rd, next_pc + 4);
        branch(true, a);
        return;
    case CachedDirect::Sync:
        return;
    case CachedDirect::Mfhi:
        write(decoded.rd, hi);
        return;
    case CachedDirect::Mthi:
        hi = a;
        return;
    case CachedDirect::Mflo:
        write(decoded.rd, lo);
        return;
    case CachedDirect::Mtlo:
        lo = a;
        return;
    case CachedDirect::Dsllv:
        write(decoded.rd, b << static_cast<unsigned>(a & 63U));
        return;
    case CachedDirect::Dsrlv:
        write(decoded.rd, b >> static_cast<unsigned>(a & 63U));
        return;
    case CachedDirect::Dsrav:
        write(decoded.rd, static_cast<u64>(signed64(b) >> static_cast<unsigned>(a & 63U)));
        return;
    case CachedDirect::Addu:
        write(decoded.rd, sign_extend32(static_cast<u32>(a + b)));
        return;
    case CachedDirect::Subu:
        write(decoded.rd, sign_extend32(static_cast<u32>(a - b)));
        return;
    case CachedDirect::And:
        write(decoded.rd, a & b);
        return;
    case CachedDirect::Or:
        write(decoded.rd, a | b);
        return;
    case CachedDirect::Xor:
        write(decoded.rd, a ^ b);
        return;
    case CachedDirect::Nor:
        write(decoded.rd, ~(a | b));
        return;
    case CachedDirect::Slt:
        write(decoded.rd, signed64(a) < signed64(b));
        return;
    case CachedDirect::Sltu:
        write(decoded.rd, a < b);
        return;
    case CachedDirect::Daddu:
        write(decoded.rd, a + b);
        return;
    case CachedDirect::Dsubu:
        write(decoded.rd, a - b);
        return;
    case CachedDirect::Dsll:
        write(decoded.rd, b << decoded.sa);
        return;
    case CachedDirect::Dsrl:
        write(decoded.rd, b >> decoded.sa);
        return;
    case CachedDirect::Dsra:
        write(decoded.rd, static_cast<u64>(signed64(b) >> decoded.sa));
        return;
    case CachedDirect::Dsll32:
        write(decoded.rd, b << (decoded.sa + 32));
        return;
    case CachedDirect::Dsrl32:
        write(decoded.rd, b >> (decoded.sa + 32));
        return;
    case CachedDirect::Dsra32:
        write(decoded.rd, static_cast<u64>(signed64(b) >> (decoded.sa + 32)));
        return;
    case CachedDirect::Bltz:
        branch(signed64(a) < 0, target);
        return;
    case CachedDirect::Bgez:
        branch(signed64(a) >= 0, target);
        return;
    case CachedDirect::Bltzal:
        write(31, sign_extend32(static_cast<u32>(next_pc + 4)));
        branch(signed64(gpr[decoded.rs]) < 0, target);
        return;
    case CachedDirect::Bgezal:
        branch(signed64(a) >= 0, target);
        write(31, sign_extend32(static_cast<u32>(next_pc + 4)));
        return;
    case CachedDirect::J:
        branch(true, (next_pc & ~0x0fffffffULL) | (static_cast<u64>(decoded.word & 0x03ffffffU) << 2));
        return;
    case CachedDirect::Jal:
        write(31, next_pc + 4);
        branch(true, (next_pc & ~0x0fffffffULL) | (static_cast<u64>(decoded.word & 0x03ffffffU) << 2));
        return;
    case CachedDirect::Beq:
        branch(a == b, target);
        return;
    case CachedDirect::Bne:
        branch(a != b, target);
        return;
    case CachedDirect::Blez:
        branch(signed64(a) <= 0, target);
        return;
    case CachedDirect::Bgtz:
        branch(signed64(a) > 0, target);
        return;
    case CachedDirect::Addiu:
        write(decoded.rt, sign_extend32(static_cast<u32>(a + immediate)));
        return;
    case CachedDirect::Slti:
        write(decoded.rt, signed64(a) < signed64(immediate));
        return;
    case CachedDirect::Sltiu:
        write(decoded.rt, a < immediate);
        return;
    case CachedDirect::Andi:
        write(decoded.rt, a & (decoded.word & 0xffffU));
        return;
    case CachedDirect::Ori:
        write(decoded.rt, a | (decoded.word & 0xffffU));
        return;
    case CachedDirect::Xori:
        write(decoded.rt, a ^ (decoded.word & 0xffffU));
        return;
    case CachedDirect::Lui:
        write(decoded.rt, sign_extend32(decoded.word << 16));
        return;
    case CachedDirect::Daddiu:
        write(decoded.rt, a + immediate);
        return;
    case CachedDirect::None:
        return;
    }
}

unsigned Cpu::batch_cached_private(unsigned maximum_steps, u64 maximum_cycles) {
    if (maximum_steps < 2 || maximum_cycles < 2 || !fetched_instruction_.valid)
        return 0;
    const auto decode = [&](u64 address, u32 instruction) -> const CachedDecode& {
        const unsigned line_index = static_cast<unsigned>((address >> 5) & 511U);
        auto& entry = cached_decode_[line_index * 8U + ((address >> 2) & 7U)];
        if (!entry.valid || entry.word != instruction) {
            cached_line_plans_[line_index].valid = false;
            entry = decode_cached_instruction(instruction);
        }
        return entry;
    };
    if (decode(pc, fetched_instruction_.instruction).kind == CachedKind::Unsupported)
        return 0;

    const auto clean_state = [&] {
        return !frozen && !nmi_pending_ && !executing_step_ && !exception_pending && !redirected_ &&
               !annul_next_ && gpr[0] == 0 && count_write_hold_ == 0 && software_interrupt_delay_ == 0 &&
               speculative_refill_count_ == 0 && wired_writes_[0].instruction == 0 &&
               wired_writes_[1].instruction == 0 && kernel_mode() && !little_endian() &&
               fetched_instruction_.valid && fetched_instruction_.address == pc;
    };
    if (!clean_state())
        return 0;

    const auto cached_word = [&](u64 address, u32& word) {
        if ((address & 0xffffffffe0000003ULL) != 0xffffffff80000000ULL)
            return false;
        const u32 physical = static_cast<u32>(address) & 0x1fffffffU;
        const auto& line = instruction_cache[(address >> 5) & 511U];
        if (!line.valid || line.tag != (physical & 0xfffff000U))
            return false;
        word = read_be32(line.data.data() + (physical & 28U));
        return true;
    };

    // Accepted instructions cannot modify I-cache, and the slice ends before
    // callbacks or cache operations. Validate each visited line once per slice.
    std::bitset<512> validated_lines;
    const auto cached_line_plan = [&](u64 address) -> CachedLinePlan* {
        if ((address & 0xffffffffe0000003ULL) != 0xffffffff80000000ULL)
            return nullptr;
        const u32 physical = static_cast<u32>(address) & 0x1fffffffU;
        const unsigned line_index = static_cast<unsigned>((address >> 5) & 511U);
        const auto& line = instruction_cache[line_index];
        const u32 tag = physical & 0xfffff000U;
        if (!line.valid || line.tag != tag)
            return nullptr;

        auto& plan = cached_line_plans_[line_index];
        if (validated_lines.test(line_index))
            return &plan;
        validated_lines.set(line_index);
        if (plan.valid && plan.tag == tag && line.data == plan.image)
            return &plan;

        plan.valid = false;
        const unsigned decode_base = line_index * 8U;
        for (unsigned slot = 0; slot < 8U; ++slot)
            cached_decode_[decode_base + slot] =
                decode_cached_instruction(read_be32(line.data.data() + slot * 4U));
        plan.image = line.data;
        plan.tag = tag;
        plan.valid = true;
        return &plan;
    };

    CacheLine<16>* data_line = nullptr;
    unsigned data_offset = 0;
    const auto data_hit = [&](const CachedDecode& decoded) {
        if (decoded.kind != CachedKind::Load && decoded.kind != CachedKind::Store)
            return true;
        const unsigned width = decoded.memory_width;
        const u64 address = gpr[decoded.rs] + sign_extend16(static_cast<u16>(decoded.word));
        if ((address & (width - 1U)) != 0 || (address & 0xffffffffe0000000ULL) != 0xffffffff80000000ULL)
            return false;
        const u32 physical = static_cast<u32>(address) & 0x1fffffffU;
        if (decoded.kind == CachedKind::Load && width == 8 && physical >= 0x04000000U)
            return false;
        auto& line = data_cache[(address >> 4) & 511U];
        if (!line.valid || line.tag != (physical & 0xfffff000U))
            return false;
        data_line = &line;
        data_offset = physical & 15U;
        return true;
    };

    const auto fpu_issue_hazard = [&](const CachedDecode& decoded) {
        if (pending_fpu_register_ >= 32)
            return false;
        const unsigned op = decoded.word >> 26;
        const unsigned cop1_operation = (decoded.word >> 21) & 31U;
        if (op != 0x11 || cop1_operation < 16)
            return false;
        const unsigned fs = (decoded.word >> 11) & 31U;
        const unsigned ft = (decoded.word >> 16) & 31U;
        return fs == pending_fpu_register_ || ft == pending_fpu_register_;
    };

    const auto operation_cycles = [&](const CachedDecode& decoded) -> unsigned {
        const unsigned op = decoded.word >> 26;
        if (op != 0x11 && !decoded.floating_memory)
            return 1;
        if ((status() & (1U << 29U)) == 0)
            return 0;
        if (op != 0x11)
            return 1;
        const unsigned cop1_operation = (decoded.word >> 21) & 31U;
        if (cop1_operation < 16)
            return 1;
        if ((decoded.word & 63U) >= 0x30U)
            return fpu.compare_nontrapping(decoded.word) ? 1U : 0U;
        return fpu.nontrapping_arithmetic_cycles(decoded.word);
    };

    u32 current_word = 0;
    u32 prefetched_word = 0;
    if (!cached_word(pc, current_word) || current_word != fetched_instruction_.instruction)
        return 0;
    const auto& first = decode(pc, current_word);
    if (first.kind == CachedKind::Unsupported || operation_cycles(first) == 0 || !data_hit(first) ||
        !cached_word(next_pc, prefetched_word))
        return 0;
    if (branches_to_self(current_word, pc) && prefetched_word == 0)
        return 0;
    // A single accepted instruction cannot amortize the slice setup. Classify the
    // already cached successor without assuming its future operands or data hit.
    if (decode(next_pc, prefetched_word).kind == CachedKind::Unsupported)
        return 0;

    const u64 device_edge = system_.cached_private_event_cycles();
    // settle() may deliver a host callback. Revalidate every assumption that callback
    // code can change before using the event bound or any cached instruction metadata.
    if (device_edge <= 2 || !clean_state() || !cached_word(pc, current_word) ||
        current_word != fetched_instruction_.instruction)
        return 0;
    CachedLinePlan* active_plan = cached_line_plan(pc);
    if (active_plan == nullptr)
        return 0;
    u64 active_line_base = pc & ~31ULL;
    unsigned active_line_index = static_cast<unsigned>((pc >> 5) & 511U);
    current_word = cached_decode_[active_line_index * 8U + ((pc >> 2) & 7U)].word;
    if (current_word != fetched_instruction_.instruction)
        return 0;
    const auto& settled_first = cached_decode_[active_line_index * 8U + ((pc >> 2) & 7U)];
    unsigned accepted_cycles = operation_cycles(settled_first);
    if (settled_first.kind == CachedKind::Unsupported || accepted_cycles == 0 || !data_hit(settled_first))
        return 0;
    if ((next_pc & 3U) == 0 && (next_pc & ~31ULL) == active_line_base)
        prefetched_word = cached_decode_[active_line_index * 8U + ((next_pc >> 2) & 7U)].word;
    else if (!cached_word(next_pc, prefetched_word))
        return 0;
    if (branches_to_self(current_word, pc) && prefetched_word == 0)
        return 0;
    // Sample interrupt acceptance after settlement callbacks. An accepting CPU
    // advances the RSP at each instruction boundary; a masked CPU can finish its
    // private register/cache work before the ordinary scheduler catches up the RSP.
    const bool run_rsp_coupled = system_.rsp.running() && (status() & 0x407U) == 0x401U;
    update_interrupt_inputs();
    if ((status() & 7U) == 1U && (static_cast<u32>(cp0[13]) & status() & 0xff00U) != 0)
        return 0;

    const u32 distance = static_cast<u32>(cp0[11]) - static_cast<u32>(cp0[9]);
    const u64 count_ticks = distance == 0 ? (1ULL << 32) : distance;
    const u64 timer_edge = count_ticks * 2 - static_cast<u64>(count_half_);
    const u64 step_limit =
        std::min(static_cast<u64>(maximum_steps), std::numeric_limits<u64>::max() - instruction_count);
    u64 cycle_limit = std::min(maximum_cycles, device_edge - 1);
    cycle_limit = std::min(cycle_limit, timer_edge - 1);
    cycle_limit = std::min(cycle_limit, std::numeric_limits<u64>::max() - cycles);
    if (step_limit < 2 || cycle_limit < 2)
        return 0;

    struct StepGuard {
        bool& active;
        explicit StepGuard(bool& selected) : active(selected) {
            active = true;
        }
        ~StepGuard() {
            active = false;
        }
    } guard{executing_step_};

    const u64 previous_instruction_cycles = instruction_cycles_;
    const u64 previous_synchronized_cycles = synchronized_instruction_cycles_;
    instruction_cycles_ = 1;
    synchronized_instruction_cycles_ = 0;
    const u64 initial_rcp_phase = system_.rcp_fraction_;
    u64 shadow_rcp_phase = initial_rcp_phase;
    [[maybe_unused]] u64 coupled_rsp_ticks = 0;
    // These cycles contain only local RSP packets, so no CPU-visible change
    // can occur before they are retired. Shared operations keep instruction-
    // boundary interrupt sampling, including multicycle FPU instructions.
    u64 pending_rsp_ticks = 0;
    u64 local_rsp_budget = run_rsp_coupled ? system_.local_rsp_cycle_budget(64) : 0;
    u64 extra_cycles = 0;
    u64 amount = std::min(step_limit, cycle_limit);
    const CachedDecode* accepted = &settled_first;
    unsigned steps = 0;
    for (;;) {
        const auto& decoded = *accepted;
        // Both issue interlocks wait only while the instruction is still in its
        // first cycle. If both encoded fields match, they share that one wait.
        const bool issue_wait =
            fpu_issue_hazard(decoded) ||
            (pending_load_register_ != 0 && (decoded.integer_reads & (1U << pending_load_register_)) != 0);
        const unsigned cost = accepted_cycles + static_cast<unsigned>(issue_wait);
        if (cost > 1U && cost > cycle_limit - steps - extra_cycles)
            break;
        u64 next_rcp_phase = shadow_rcp_phase;
        u64 rsp_ticks = 0;
        if (run_rsp_coupled) {
            if (cost == 1U) {
                const u64 phase_sum = shadow_rcp_phase + 2U;
                rsp_ticks = static_cast<u64>(phase_sum >= 3U);
                next_rcp_phase = phase_sum - rsp_ticks * 3U;
            } else {
                const u64 phase_sum = shadow_rcp_phase + static_cast<u64>(cost) * 2U;
                rsp_ticks = phase_sum / 3U;
                next_rcp_phase = phase_sum % 3U;
            }
        }

        instruction_cycles_ = 1U + static_cast<unsigned>(issue_wait);
        pending_load_register_ = 0;
        pending_fpu_register_ = 32;
        following_pc_ = next_pc + 4;
        following_delay_slot_ = annul_next_ = false;
        fetched_instruction_.valid = false;
        if (decoded.kind == CachedKind::Load || decoded.kind == CachedKind::Store)
            execute_cached_memory(decoded, *data_line, data_offset);
        else if (decoded.kind == CachedKind::Private && decoded.direct != CachedDirect::None)
            execute_cached_direct(decoded);
        else
            execute(current_word);
        // Live operand preflight proves the exact latency and excludes traps and
        // internal synchronization. Arithmetic still uses the ordinary FPU path.
        assert(!exception_pending && !redirected_ && !frozen && !annul_next_ && instruction_cycles_ == cost &&
               synchronized_instruction_cycles_ == 0);
        pending_load_register_ = decoded.load_target < 32 ? static_cast<unsigned>(decoded.load_target) : 0U;
        if ((decoded.word >> 26U) == 0x11U && ((decoded.word >> 21U) & 31U) >= 16U &&
            (decoded.word & 63U) < 0x30U)
            pending_fpu_register_ = (decoded.word >> 6U) & 31U;
        pc = next_pc;
        next_pc = following_pc_;
        in_delay_slot_ = following_delay_slot_;
        fetched_instruction_ = {pc, prefetched_word, true};
        if (rsp_ticks != 0) {
            if (rsp_ticks <= local_rsp_budget) {
                pending_rsp_ticks += rsp_ticks;
                local_rsp_budget -= rsp_ticks;
            } else {
                system_.advance_cached_rsp_ticks(pending_rsp_ticks + rsp_ticks);
                pending_rsp_ticks = 0;
                local_rsp_budget = system_.local_rsp_cycle_budget(64);
            }
        }
        coupled_rsp_ticks += rsp_ticks;
        shadow_rcp_phase = next_rcp_phase;
        if (cost > 1U) {
            extra_cycles += cost - 1U;
            amount = std::min(step_limit, cycle_limit - extra_cycles);
        }
        ++steps;
        // An interrupt raised and cleared during one multicycle instruction is
        // sampled only after that entire instruction's RSP time has elapsed.
        if (steps == amount || (run_rsp_coupled && system_.bus.interrupt_pending()))
            break;

        if (!fetched_instruction_.valid || fetched_instruction_.address != pc)
            break;
        const u64 line_base = pc & ~31ULL;
        if (active_plan == nullptr || line_base != active_line_base) {
            active_plan = cached_line_plan(pc);
            if (active_plan == nullptr)
                break;
            active_line_base = line_base;
            active_line_index = static_cast<unsigned>((pc >> 5) & 511U);
        }
        current_word = cached_decode_[active_line_index * 8U + ((pc >> 2) & 7U)].word;
        if (current_word != fetched_instruction_.instruction)
            break;
        const auto& next_decoded = cached_decode_[active_line_index * 8U + ((pc >> 2) & 7U)];
        accepted_cycles = operation_cycles(next_decoded);
        if (next_decoded.kind == CachedKind::Unsupported || accepted_cycles == 0 || !data_hit(next_decoded))
            break;
        if ((next_pc & 3U) == 0 && (next_pc & ~31ULL) == active_line_base)
            prefetched_word = cached_decode_[active_line_index * 8U + ((next_pc >> 2) & 7U)].word;
        else if (!cached_word(next_pc, prefetched_word))
            break;
        if (branches_to_self(current_word, pc) && prefetched_word == 0)
            break;

        accepted = &next_decoded;
    }

    if (steps == 0) {
        instruction_cycles_ = previous_instruction_cycles;
        synchronized_instruction_cycles_ = previous_synchronized_cycles;
        return 0;
    }
    advance_batched_instruction_counters(steps);
    batched_cached_instructions_ += steps;
    const u64 elapsed_cycles = steps + extra_cycles;
    if (run_rsp_coupled) {
        if (pending_rsp_ticks != 0)
            system_.advance_cached_rsp_ticks(pending_rsp_ticks);
        const u64 fraction = (elapsed_cycles % 3U) * 2U + initial_rcp_phase;
        [[maybe_unused]] const u64 expected_rsp_ticks = (elapsed_cycles / 3U) * 2U + fraction / 3U;
        assert(coupled_rsp_ticks == expected_rsp_ticks);
        assert(shadow_rcp_phase == fraction % 3U);
        advance_clock_counters(elapsed_cycles);
        system_.finish_rsp_slice(elapsed_cycles, true);
        assert(system_.rcp_fraction_ == shadow_rcp_phase);
        update_interrupt_inputs();
    } else {
        update_clocks(elapsed_cycles);
    }
    synchronized_instruction_cycles_ = instruction_cycles_;
    return steps;
}

} // namespace cupid
