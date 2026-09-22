#include "cupid/system.hpp"

#include <algorithm>
#include <limits>

namespace cupid {
namespace {

bool add_overflows(u64 a, u64 b, u64 result, unsigned bits) {
    return ((~(a ^ b) & (a ^ result)) >> (bits - 1) & 1U) != 0;
}

bool subtract_overflows(u64 a, u64 b, u64 result, unsigned bits) {
    return (((a ^ b) & (a ^ result)) >> (bits - 1) & 1U) != 0;
}

struct Product {
    u64 low;
    u64 high;
};

Product multiply64(u64 a, u64 b) {
    const u64 a0 = static_cast<u32>(a);
    const u64 a1 = a >> 32;
    const u64 b0 = static_cast<u32>(b);
    const u64 b1 = b >> 32;
    const u64 low_product = a0 * b0;
    const u64 cross = a1 * b0 + (low_product >> 32);
    const u64 middle = a0 * b1 + static_cast<u32>(cross);
    return {
        (middle << 32) | static_cast<u32>(low_product),
        a1 * b1 + (cross >> 32) + (middle >> 32),
    };
}

} // namespace

Cpu::Cpu(System& system) : fpu(*this), system_(system) {}

void Cpu::reset() {
    gpr.fill(0);
    cp0.fill(0);
    tlb.fill({});
    data_cache.fill({});
    instruction_cache.fill({});
    fpu.reset();
    hi = lo = cycles = instruction_count = cop2_latch = cop0_latch_ = 0;
    linked = exception_pending = frozen = count_half_ = redirected_ = false;
    random_ = 31;
    random_wired_ = software_interrupt_delay_ = 0;
    wired_writes_.fill({});
    pending_load_register_ = 0;
    count_write_hold_ = 0;
    instruction_cycles_ = 1;
    synchronized_instruction_cycles_ = 0;
    executing_step_ = false;
    nmi_pending_ = false;
    batched_idle_instructions_ = 0;
    speculative_fetch_ = false;
    speculative_refill_bases_.fill(0);
    speculative_refill_count_ = 0;
    write_buffer_.fill({});
    write_buffer_head_ = write_buffer_count_ = 0;
    cp0[12] = 0x3440ff04U;
    cp0[15] = 0x00000b22U;
    cp0[16] = 0x7006e460U;
    gpr[29] = 0xffffffffa4001ff0ULL;
    set_pc(0xffffffffbfc00000ULL);
}

bool Cpu::latch_cached_instruction(u64 address) {
    if (little_endian() || (address & 3U) != 0)
        return false;
    const u32 low = static_cast<u32>(address);
    if (address != sign_extend32(low) || !kernel_mode() || low < 0x80000000U || low >= 0xa0000000U)
        return false;
    const u32 physical = low & 0x1fffffffU;
    const auto& line = instruction_cache[(address >> 5) & 511U];
    if (!line.valid || line.tag != (physical & 0xfffff000U))
        return false;
    fetched_instruction_.address = address;
    fetched_instruction_.instruction = read_be32(line.data.data() + (physical & 28U));
    fetched_instruction_.valid = true;
    return true;
}

void Cpu::set_pc(u64 address) {
    fetched_instruction_.valid = false;
    pc = address;
    next_pc = address + 4;
    following_pc_ = address + 8;
    in_delay_slot_ = following_delay_slot_ = annul_next_ = false;
    pending_load_register_ = 0;
    pending_fpu_register_ = 32;
}

bool Cpu::require_coprocessor(unsigned coprocessor) {
    if ((coprocessor == 0 && kernel_mode()) || (status() & (1U << (28 + coprocessor))) != 0) {
        return true;
    }
    raise_exception(Exception::CoprocessorUnusable, coprocessor);
    return false;
}

void Cpu::raise_exception(Exception exception, unsigned coprocessor, bool refill, bool instruction_fetch) {
    if (speculative_fetch_)
        return;
    if (exception == Exception::FloatingPoint && executing_step_)
        coprocessor = sample_exception_coprocessor();
    // Redirect latency depends on whether the fault is detected in RF, EX, or DC.
    if (instruction_fetch || exception == Exception::BusInstruction) {
        add_cycles(2);
    } else if (exception == Exception::Syscall || exception == Exception::Breakpoint ||
               exception == Exception::ReservedInstruction || exception == Exception::CoprocessorUnusable) {
        add_cycles(3);
    } else {
        add_cycles(4);
    }
    const bool already_exl = (status() & 2U) != 0;
    const u64 vector = (status() & 0x00400000U) != 0 ? 0xffffffffbfc00200ULL : 0xffffffff80000000ULL;
    const u64 offset = refill && !already_exl ? (wide_addressing() ? 0x80U : 0U) : 0x180U;
    u32 cause = static_cast<u32>(cp0[13]);
    cause = (cause & ~0x3000007cU) | (static_cast<u32>(exception) << 2) | ((coprocessor & 3U) << 28);
    if (!already_exl) {
        cp0[14] = pc - (in_delay_slot_ ? 4U : 0U);
        cause = (cause & ~0x80000000U) | (in_delay_slot_ ? 0x80000000U : 0U);
    }
    cp0[13] = cause;
    cp0[12] |= 2U;
    set_pc(vector + offset);
    exception_pending = true;
}

void Cpu::branch(bool condition, u64 target, bool likely) {
    if (condition) {
        following_pc_ = target;
        following_delay_slot_ = true;
    } else if (likely) {
        annul_next_ = true;
    } else {
        following_delay_slot_ = true;
    }
}

void Cpu::update_clocks(u64 elapsed) {
    const u64 held = std::min(elapsed, count_write_hold_);
    count_write_hold_ -= held;
    const u64 count_cycles = elapsed - held;
    const u64 ticks = count_cycles / 2 + ((count_cycles % 2 + static_cast<u64>(count_half_)) / 2);
    count_half_ = ((count_cycles & 1U) != 0) != count_half_;
    const u32 old_count = static_cast<u32>(cp0[9]);
    const u32 distance = static_cast<u32>(cp0[11]) - old_count;
    if ((distance != 0 && ticks >= distance) || ticks >= (1ULL << 32))
        cp0[13] |= 0x8000U;
    cp0[9] = static_cast<u32>(old_count + ticks);
    cycles += elapsed;
    system_.advance_deferred(elapsed);
    update_interrupt_inputs();
}

void Cpu::update_interrupt_inputs() {
    cp0[13] = (cp0[13] & ~0x1400ULL) | (system_.bus.interrupt_pending() ? 0x400U : 0U) |
              (system_.bus.pif_boot.pre_nmi() ? 0x1000U : 0U);
}

void Cpu::step() {
    struct StepGuard {
        bool& active;
        ~StepGuard() {
            active = false;
        }
    } guard{executing_step_};
    executing_step_ = true;
    speculative_refill_count_ = 0;
    exception_pending = redirected_ = false;
    instruction_cycles_ = 1;
    synchronized_instruction_cycles_ = 0;
    if (nmi_pending_) {
        accept_nmi();
        synchronize();
        return;
    }
    if (frozen) {
        synchronize();
        return;
    }
    update_interrupt_inputs();
    u32 pending_interrupts = static_cast<u32>(cp0[13]) & status() & 0xff00U;
    if (software_interrupt_delay_ != 0) {
        --software_interrupt_delay_;
        pending_interrupts &= ~0x300U;
    }
    if ((status() & 7U) == 1U && pending_interrupts != 0) {
        raise_exception(Exception::Interrupt);
        synchronize();
        return;
    }
    u64 instruction = 0;
    if (fetch_instruction(instruction)) {
        following_pc_ = next_pc + 4;
        following_delay_slot_ = annul_next_ = false;
        begin_instruction_timing(static_cast<u32>(instruction));
        if (!latch_cached_instruction(next_pc))
            prefetch_instruction(next_pc, true);
        execute(static_cast<u32>(instruction));
        finish_instruction_timing(static_cast<u32>(instruction));
        ++instruction_count;
        random_ = random_ == random_wired_ ? 31U : (random_ - 1U) & 63U;
        auto& wired_write = wired_writes_[instruction_count & 1U];
        if (wired_write.instruction == instruction_count) {
            random_ = 31;
            random_wired_ = wired_write.value;
            wired_write = {};
        }
        if (!exception_pending && !redirected_ && !frozen) {
            if (annul_next_) {
                add_cycles(1);
                pending_load_register_ = 0;
                pending_fpu_register_ = 32;
                pc = next_pc + 4;
                next_pc = pc + 4;
                in_delay_slot_ = false;
            } else {
                pc = next_pc;
                next_pc = following_pc_;
                in_delay_slot_ = following_delay_slot_;
            }
        }
    }
    gpr[0] = 0;
    complete_speculative_refills();
    synchronize();
}

void Cpu::begin_instruction_timing(u32 instruction) {
    const unsigned op = instruction >> 26;
    const unsigned rs = (instruction >> 21) & 31U;
    const unsigned rt = (instruction >> 16) & 31U;
    const unsigned fs = (instruction >> 11) & 31U;
    if (op == 0x11 && rs >= 16 && pending_fpu_register_ < 32 && instruction_cycles_ == 1 &&
        (fs == pending_fpu_register_ || rt == pending_fpu_register_)) {
        add_cycles(1);
    }
    pending_fpu_register_ = 32;
    bool check_rs = op != 2 && op != 3;
    bool check_rt = check_rs;
    if (op == 0x11 || op == 0x12) {
        check_rs = false;
        check_rt = rs < 8;
    } else if (op == 0x31 || op == 0x35 || op == 0x39 || op == 0x3d) {
        check_rt = false;
    }
    // The integer issue interlock compares encoded fields, including immediate destinations.
    if (pending_load_register_ != 0 && instruction_cycles_ == 1 &&
        ((check_rs && rs == pending_load_register_) || (check_rt && rt == pending_load_register_))) {
        add_cycles(1);
    }
    pending_load_register_ = 0;
}

void Cpu::finish_instruction_timing(u32 instruction) {
    if (exception_pending || frozen || redirected_)
        return;
    const unsigned op = instruction >> 26;
    const unsigned rs = (instruction >> 21) & 31U;
    if (op == 0x11 && rs >= 16 && (instruction & 63U) < 0x30) {
        pending_fpu_register_ = (instruction >> 6) & 31U;
    }
    if ((op >= 0x20 && op <= 0x27) || op == 0x1a || op == 0x1b || op == 0x30 || op == 0x34 || op == 0x37 ||
        (op == 0x10 && rs <= 1)) {
        pending_load_register_ = (instruction >> 16) & 31U;
    }
}

void Cpu::execute(u32 instruction) {
    struct ZeroGuard {
        u64& zero;
        ~ZeroGuard() {
            zero = 0;
        }
    } guard{gpr[0]};
    gpr[0] = 0;
    const unsigned op = instruction >> 26;
    const unsigned rs = (instruction >> 21) & 31U;
    const unsigned rt = (instruction >> 16) & 31U;
    const u64 a = gpr[rs];
    const u64 b = gpr[rt];
    const u64 immediate = sign_extend16(static_cast<u16>(instruction));
    const u64 address = a + immediate;
    const u64 target = next_pc + immediate * 4;
    u64 value = 0;

    switch (op) {
    case 0x00:
        execute_special(instruction);
        return;
    case 0x01:
        execute_regimm(instruction);
        return;
    case 0x02:
        branch(true, (next_pc & ~0x0fffffffULL) | (static_cast<u64>(instruction & 0x03ffffffU) << 2));
        return;
    case 0x03:
        gpr[31] = next_pc + 4;
        branch(true, (next_pc & ~0x0fffffffULL) | (static_cast<u64>(instruction & 0x03ffffffU) << 2));
        return;
    case 0x04:
        branch(a == b, target);
        return;
    case 0x05:
        branch(a != b, target);
        return;
    case 0x06:
        branch(signed64(a) <= 0, target);
        return;
    case 0x07:
        branch(signed64(a) > 0, target);
        return;
    case 0x08:
        if (add_overflows(a, immediate, address, 32))
            raise_exception(Exception::Overflow);
        else
            gpr[rt] = sign_extend32(static_cast<u32>(address));
        return;
    case 0x09:
        gpr[rt] = sign_extend32(static_cast<u32>(address));
        return;
    case 0x0a:
        gpr[rt] = signed64(a) < signed64(immediate);
        return;
    case 0x0b:
        gpr[rt] = a < immediate;
        return;
    case 0x0c:
        gpr[rt] = a & (instruction & 0xffffU);
        return;
    case 0x0d:
        gpr[rt] = a | (instruction & 0xffffU);
        return;
    case 0x0e:
        gpr[rt] = a ^ (instruction & 0xffffU);
        return;
    case 0x0f:
        gpr[rt] = sign_extend32(instruction << 16);
        return;
    case 0x10:
        execute_cop0(instruction);
        return;
    case 0x11:
        fpu.execute(instruction);
        return;
    case 0x12:
        execute_cop2(instruction);
        return;
    case 0x13:
    case 0x33:
    case 0x3b:
        raise_exception(Exception::ReservedInstruction);
        return;
    case 0x14:
        branch(a == b, target, true);
        return;
    case 0x15:
        branch(a != b, target, true);
        return;
    case 0x16:
        branch(signed64(a) <= 0, target, true);
        return;
    case 0x17:
        branch(signed64(a) > 0, target, true);
        return;
    case 0x18:
    case 0x19:
        if (!wide_instructions())
            raise_exception(Exception::ReservedInstruction);
        else if (op == 0x18 && add_overflows(a, immediate, address, 64))
            raise_exception(Exception::Overflow);
        else
            gpr[rt] = address;
        return;
    case 0x1a:
    case 0x1b:
        if (!wide_instructions())
            raise_exception(Exception::ReservedInstruction);
        else
            load_partial(address, 8, op == 0x1a, rt);
        return;
    case 0x20:
        if (read_memory(address, 1, value))
            gpr[rt] = sign_extend8(static_cast<u8>(value));
        return;
    case 0x21:
        if (read_memory(address, 2, value))
            gpr[rt] = sign_extend16(static_cast<u16>(value));
        return;
    case 0x22:
        load_partial(address, 4, true, rt);
        return;
    case 0x23:
        if (read_memory(address, 4, value))
            gpr[rt] = sign_extend32(static_cast<u32>(value));
        return;
    case 0x24:
        if (read_memory(address, 1, value))
            gpr[rt] = value;
        return;
    case 0x25:
        if (read_memory(address, 2, value))
            gpr[rt] = value;
        return;
    case 0x26:
        load_partial(address, 4, false, rt);
        return;
    case 0x27:
        if (read_memory(address, 4, value))
            gpr[rt] = value;
        return;
    case 0x28:
        write_memory(address, 1, b);
        return;
    case 0x29:
        write_memory(address, 2, b);
        return;
    case 0x2a:
        store_partial(address, 4, true, b);
        return;
    case 0x2b:
        write_memory(address, 4, b);
        return;
    case 0x2c:
    case 0x2d:
        if (!wide_instructions())
            raise_exception(Exception::ReservedInstruction);
        else
            store_partial(address, 8, op == 0x2c, b);
        return;
    case 0x2e:
        store_partial(address, 4, false, b);
        return;
    case 0x2f:
        cache_operation(rt, address);
        return;
    case 0x30:
    case 0x34: {
        if (op == 0x34 && !wide_instructions()) {
            raise_exception(Exception::ReservedInstruction);
            return;
        }
        if (!read_memory(address, op == 0x30 ? 4U : 8U, value))
            return;
        u32 physical = 0;
        bool cached = false;
        if (!translate(address, Access::Read, physical, cached))
            return;
        gpr[rt] = op == 0x30 ? sign_extend32(static_cast<u32>(value)) : value;
        cp0[17] = physical >> 4;
        linked = true;
        return;
    }
    case 0x31:
    case 0x35:
        if (!require_coprocessor(1))
            return;
        if (read_memory(address, op == 0x31 ? 4U : 8U, value)) {
            if (op == 0x31)
                fpu.write_word(rt, static_cast<u32>(value));
            else
                fpu.write_doubleword(rt, value);
        }
        return;
    case 0x32:
    case 0x36:
        if (!require_coprocessor(2))
            return;
        if (read_memory(address & ~7ULL, 8, value))
            cop2_latch = value;
        return;
    case 0x37:
        if (!wide_instructions())
            raise_exception(Exception::ReservedInstruction);
        else if (read_memory(address, 8, value))
            gpr[rt] = value;
        return;
    case 0x38:
    case 0x3c:
        if (op == 0x3c && !wide_instructions()) {
            raise_exception(Exception::ReservedInstruction);
            return;
        }
        if (!linked)
            gpr[rt] = 0;
        else
            gpr[rt] = write_memory(address, op == 0x38 ? 4U : 8U, b) ? 1U : 0U;
        return;
    case 0x39:
    case 0x3d:
        if (!require_coprocessor(1))
            return;
        write_memory(address, op == 0x39 ? 4U : 8U, op == 0x39 ? fpu.read_word(rt) : fpu.read_doubleword(rt));
        return;
    case 0x3a:
    case 0x3e:
        if (!require_coprocessor(2))
            return;
        write_memory(address, op == 0x3a ? 4U : 8U, op == 0x3a ? static_cast<u32>(cop2_latch) : cop2_latch);
        return;
    case 0x3f:
        if (!wide_instructions())
            raise_exception(Exception::ReservedInstruction);
        else
            write_memory(address, 8, b);
        return;
    default:
        raise_exception(Exception::ReservedInstruction);
        return;
    }
}

void Cpu::execute_special(u32 instruction) {
    const unsigned rs = (instruction >> 21) & 31U;
    const unsigned rt = (instruction >> 16) & 31U;
    const unsigned rd = (instruction >> 11) & 31U;
    const unsigned sa = (instruction >> 6) & 31U;
    const unsigned op = instruction & 63U;
    const u64 a = gpr[rs];
    const u64 b = gpr[rt];
    const unsigned variable32 = static_cast<unsigned>(a & 31U);
    const unsigned variable64 = static_cast<unsigned>(a & 63U);
    const bool wide = op == 0x14 || op == 0x16 || op == 0x17 || (op >= 0x1c && op <= 0x1f) ||
                      (op >= 0x2c && op <= 0x2f) || op == 0x38 || op == 0x3a || op == 0x3b || op == 0x3c ||
                      op == 0x3e || op == 0x3f;
    if (wide && !wide_instructions()) {
        raise_exception(Exception::ReservedInstruction);
        return;
    }

    switch (op) {
    case 0x00:
        gpr[rd] = sign_extend32(static_cast<u32>(b) << sa);
        return;
    case 0x02:
        gpr[rd] = sign_extend32(static_cast<u32>(b) >> sa);
        return;
    case 0x03:
        gpr[rd] = sign_extend32(static_cast<u32>(signed64(b) >> sa));
        return;
    case 0x04:
        gpr[rd] = sign_extend32(static_cast<u32>(b) << variable32);
        return;
    case 0x06:
        gpr[rd] = sign_extend32(static_cast<u32>(b) >> variable32);
        return;
    case 0x07:
        gpr[rd] = sign_extend32(static_cast<u32>(signed64(b) >> variable32));
        return;
    case 0x08:
        branch(true, a);
        return;
    case 0x09:
        gpr[rd] = next_pc + 4;
        branch(true, a);
        return;
    case 0x0c:
        raise_exception(Exception::Syscall);
        return;
    case 0x0d:
        raise_exception(Exception::Breakpoint);
        return;
    case 0x0f:
        return;
    case 0x10:
        gpr[rd] = hi;
        return;
    case 0x11:
        hi = a;
        return;
    case 0x12:
        gpr[rd] = lo;
        return;
    case 0x13:
        lo = a;
        return;
    case 0x14:
        gpr[rd] = b << variable64;
        return;
    case 0x16:
        gpr[rd] = b >> variable64;
        return;
    case 0x17:
        gpr[rd] = static_cast<u64>(signed64(b) >> variable64);
        return;
    case 0x18:
    case 0x19: {
        const u64 rhs35 = static_cast<u64>(signed64(b << 29) >> 29);
        const u64 product =
            op == 0x18 ? a * rhs35 : static_cast<u64>(static_cast<u32>(a)) * static_cast<u32>(b);
        lo = sign_extend32(static_cast<u32>(product));
        hi = sign_extend32(static_cast<u32>(product >> 32));
        complete_multicycle_instruction(4);
        return;
    }
    case 0x1a: {
        const s64 dividend = signed32(static_cast<u32>(a));
        const s64 divisor = signed64(b);
        lo = divisor != 0 ? sign_extend32(static_cast<u32>(dividend / divisor))
                          : (dividend < 0 ? 1ULL : ~0ULL);
        hi = divisor != 0 ? sign_extend32(static_cast<u32>(dividend % divisor)) : static_cast<u64>(dividend);
        complete_multicycle_instruction(36);
        return;
    }
    case 0x1b: {
        const u32 divisor = static_cast<u32>(b);
        lo = divisor != 0 ? sign_extend32(static_cast<u32>(a) / divisor) : ~0ULL;
        hi = divisor != 0 ? sign_extend32(static_cast<u32>(a) % divisor) : sign_extend32(static_cast<u32>(a));
        complete_multicycle_instruction(36);
        return;
    }
    case 0x1c:
    case 0x1d: {
        Product product = multiply64(a, b);
        if (op == 0x1c) {
            if (signed64(a) < 0)
                product.high -= b;
            if (signed64(b) < 0)
                product.high -= a;
        }
        lo = product.low;
        hi = product.high;
        complete_multicycle_instruction(7);
        return;
    }
    case 0x1e:
        if (b == 0) {
            lo = signed64(a) < 0 ? 1ULL : ~0ULL;
            hi = a;
        } else if (a == (1ULL << 63) && b == ~0ULL) {
            lo = a;
            hi = 0;
        } else {
            lo = static_cast<u64>(signed64(a) / signed64(b));
            hi = static_cast<u64>(signed64(a) % signed64(b));
        }
        complete_multicycle_instruction(68);
        return;
    case 0x1f:
        lo = b != 0 ? a / b : ~0ULL;
        hi = b != 0 ? a % b : a;
        complete_multicycle_instruction(68);
        return;
    case 0x20:
    case 0x21:
        if (op == 0x20 && add_overflows(a, b, a + b, 32))
            raise_exception(Exception::Overflow);
        else
            gpr[rd] = sign_extend32(static_cast<u32>(a + b));
        return;
    case 0x22:
    case 0x23:
        if (op == 0x22 && subtract_overflows(a, b, a - b, 32))
            raise_exception(Exception::Overflow);
        else
            gpr[rd] = sign_extend32(static_cast<u32>(a - b));
        return;
    case 0x24:
        gpr[rd] = a & b;
        return;
    case 0x25:
        gpr[rd] = a | b;
        return;
    case 0x26:
        gpr[rd] = a ^ b;
        return;
    case 0x27:
        gpr[rd] = ~(a | b);
        return;
    case 0x2a:
        gpr[rd] = signed64(a) < signed64(b);
        return;
    case 0x2b:
        gpr[rd] = a < b;
        return;
    case 0x2c:
    case 0x2d:
        if (op == 0x2c && add_overflows(a, b, a + b, 64))
            raise_exception(Exception::Overflow);
        else
            gpr[rd] = a + b;
        return;
    case 0x2e:
    case 0x2f:
        if (op == 0x2e && subtract_overflows(a, b, a - b, 64))
            raise_exception(Exception::Overflow);
        else
            gpr[rd] = a - b;
        return;
    case 0x30:
        if (signed64(a) >= signed64(b))
            raise_exception(Exception::Trap);
        return;
    case 0x31:
        if (a >= b)
            raise_exception(Exception::Trap);
        return;
    case 0x32:
        if (signed64(a) < signed64(b))
            raise_exception(Exception::Trap);
        return;
    case 0x33:
        if (a < b)
            raise_exception(Exception::Trap);
        return;
    case 0x34:
        if (a == b)
            raise_exception(Exception::Trap);
        return;
    case 0x36:
        if (a != b)
            raise_exception(Exception::Trap);
        return;
    case 0x38:
        gpr[rd] = b << sa;
        return;
    case 0x3a:
        gpr[rd] = b >> sa;
        return;
    case 0x3b:
        gpr[rd] = static_cast<u64>(signed64(b) >> sa);
        return;
    case 0x3c:
        gpr[rd] = b << (sa + 32);
        return;
    case 0x3e:
        gpr[rd] = b >> (sa + 32);
        return;
    case 0x3f:
        gpr[rd] = static_cast<u64>(signed64(b) >> (sa + 32));
        return;
    default:
        raise_exception(Exception::ReservedInstruction);
        return;
    }
}

void Cpu::execute_regimm(u32 instruction) {
    const unsigned rs = (instruction >> 21) & 31U;
    const unsigned op = (instruction >> 16) & 31U;
    const u64 value = gpr[rs];
    const u64 immediate = sign_extend16(static_cast<u16>(instruction));
    const u64 target = next_pc + immediate * 4;
    switch (op) {
    case 0x00:
        branch(signed64(value) < 0, target);
        return;
    case 0x01:
        branch(signed64(value) >= 0, target);
        return;
    case 0x02:
        branch(signed64(value) < 0, target, true);
        return;
    case 0x03:
        branch(signed64(value) >= 0, target, true);
        return;
    case 0x08:
        if (signed64(value) >= signed64(immediate))
            raise_exception(Exception::Trap);
        return;
    case 0x09:
        if (value >= immediate)
            raise_exception(Exception::Trap);
        return;
    case 0x0a:
        if (signed64(value) < signed64(immediate))
            raise_exception(Exception::Trap);
        return;
    case 0x0b:
        if (value < immediate)
            raise_exception(Exception::Trap);
        return;
    case 0x0c:
        if (value == immediate)
            raise_exception(Exception::Trap);
        return;
    case 0x0e:
        if (value != immediate)
            raise_exception(Exception::Trap);
        return;
    case 0x10:
    case 0x12:
    case 0x13:
        gpr[31] = sign_extend32(static_cast<u32>(next_pc + 4));
        branch(op == 0x13 ? signed64(gpr[rs]) >= 0 : signed64(gpr[rs]) < 0, target, op != 0x10);
        return;
    case 0x11:
        branch(signed64(value) >= 0, target);
        gpr[31] = sign_extend32(static_cast<u32>(next_pc + 4));
        return;
    default:
        raise_exception(Exception::ReservedInstruction);
        return;
    }
}

void Cpu::execute_cop2(u32 instruction) {
    if (!require_coprocessor(2))
        return;
    const unsigned rt = (instruction >> 16) & 31U;
    switch ((instruction >> 21) & 31U) {
    case 0:
    case 2:
        gpr[rt] = sign_extend32(static_cast<u32>(cop2_latch));
        return;
    case 1:
        gpr[rt] = cop2_latch;
        return;
    case 4:
    case 5:
    case 6:
        cop2_latch = gpr[rt];
        return;
    default:
        raise_exception(Exception::ReservedInstruction, 2);
        return;
    }
}

} // namespace cupid
