#include "cupid/cpu/native.hpp"

#include "cupid/cpu.hpp"
#include "native/control.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#if defined(CUPID_CPU_NATIVE)
#include <sljitLir.h>
#endif

namespace cupid {

bool CpuNativeCode::available() {
#if defined(CUPID_CPU_NATIVE)
    return true;
#else
    return false;
#endif
}

bool CpuNativeCode::terminal_branch(u32 instruction) {
    const unsigned opcode = instruction >> 26U;
    return (opcode >= 2U && opcode <= 7U) || (opcode == 1U && ((instruction >> 16U) & 31U) <= 1U) ||
           (opcode == 0U && ((instruction & 63U) == 8U || (instruction & 63U) == 9U));
}

bool CpuNativeCode::supports(u32 instruction) {
    if (terminal_branch(instruction))
        return true;
    switch (instruction >> 26U) {
    case 0x00:
        switch (instruction & 63U) {
        case 0x00:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x06:
        case 0x07:
        case 0x0f:
        case 0x14:
        case 0x16:
        case 0x17:
        case 0x21:
        case 0x23:
        case 0x24:
        case 0x25:
        case 0x26:
        case 0x27:
        case 0x2a:
        case 0x2b:
        case 0x2d:
        case 0x2f:
        case 0x38:
        case 0x3a:
        case 0x3b:
        case 0x3c:
        case 0x3e:
        case 0x3f:
            return true;
        default:
            return false;
        }
    case 0x09:
    case 0x0a:
    case 0x0b:
    case 0x0c:
    case 0x0d:
    case 0x0e:
    case 0x0f:
    case 0x19:
    case 0x20:
    case 0x21:
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x27:
    case 0x28:
    case 0x29:
    case 0x2b:
    case 0x37:
    case 0x3f:
        return true;
    default:
        return false;
    }
}

CpuNativeCode::~CpuNativeCode() {
#if defined(CUPID_CPU_NATIVE)
    if (code_ != nullptr)
        sljit_free_code(code_, nullptr);
#endif
}

// Generated entry points have no compiler-emitted UBSan function-type prefix.
#if defined(__clang__)
__attribute__((no_sanitize("function")))
#endif
bool CpuNativeCode::execute(CpuNativeState& state) const {
    using Entry = std::intptr_t (*)(CpuNativeState*);
    state.store_count = 0;
    state.branch_taken = 0;
    state.branch_target = 0;
    if (reinterpret_cast<Entry>(code_)(&state) == 0)
        return false;
    state.commit_stores();
    return true;
}

bool CpuNativeCode::execute(std::span<u64, 32> registers) const {
    if (links_)
        return false;
    CpuNativeState state{registers.data(), nullptr};
    return execute(state);
}

std::shared_ptr<const CpuNativeCode> CpuNativeCode::compile(std::span<const u32> instructions) {
#if !defined(CUPID_CPU_NATIVE)
    (void)instructions;
    return {};
#else
    if (instructions.empty() || instructions.size() > maximum_instructions ||
        !std::all_of(instructions.begin(), instructions.end(), supports))
        return {};
    const std::unique_ptr<sljit_compiler, decltype(&sljit_free_compiler)> owner(
        sljit_create_compiler(nullptr), sljit_free_compiler);
    auto* compiler = owner.get();
    if (compiler == nullptr)
        return {};

    if (std::any_of(instructions.begin(), instructions.end() - 1, terminal_branch))
        return {};
    const bool has_branch = terminal_branch(instructions.back());

    static_assert(std::is_standard_layout_v<CacheLine<16>>);
    static_assert(offsetof(CacheLine<16>, data) == 0);
    std::array<unsigned, 32> uses{};
    bool has_memory = false;
    bool has_store = false;
    for (const u32 instruction : instructions) {
        const unsigned opcode = instruction >> 26U;
        const unsigned function = instruction & 63U;
        const bool load = opcode == 0x20U || opcode == 0x21U || opcode == 0x23U || opcode == 0x24U ||
                          opcode == 0x25U || opcode == 0x27U || opcode == 0x37U;
        const bool store = opcode == 0x28U || opcode == 0x29U || opcode == 0x2bU || opcode == 0x3fU;
        // Loads after a staged store need forwarding and retain ordinary execution.
        if (has_store && load)
            return {};
        has_memory = has_memory || load || store;
        has_store = has_store || store;
        if (opcode == 0U && function == 0x0fU)
            continue;
        const bool fixed_shift = opcode == 0U && (function <= 3U || function >= 0x38U);
        if (opcode != 0x0fU && !fixed_shift)
            ++uses[(instruction >> 21U) & 31U];
        ++uses[(instruction >> 16U) & 31U];
        if (opcode == 0U)
            ++uses[(instruction >> 11U) & 31U];
    }
    uses[0] = 0;
    constexpr sljit_s32 gpr_base = SLJIT_S0;
    constexpr sljit_s32 cache_base = SLJIT_S1;
    constexpr sljit_s32 state_base = SLJIT_S2;
    const unsigned base_registers = has_store || has_branch ? 3U : has_memory ? 2U : 1U;
    const unsigned cache_capacity = SLJIT_NUMBER_OF_SAVED_REGISTERS - base_registers;
    std::array<sljit_s32, 32> cached{};
    std::array<bool, 32> initialized{};
    std::array<bool, 32> dirty{};
    unsigned cache_count = 0;
    while (cache_count < cache_capacity) {
        const auto most_used = std::max_element(uses.begin(), uses.end());
        if (*most_used < 5U)
            break;
        const auto reg = static_cast<unsigned>(most_used - uses.begin());
        cached[reg] = SLJIT_S0 - static_cast<sljit_s32>(base_registers + cache_count++);
        *most_used = 0;
    }
    sljit_emit_enter(compiler, 0, SLJIT_ARGS1(W, P), has_memory ? 4 : 2,
                     static_cast<sljit_s32>(base_registers + cache_count), 0);
    if (has_store || has_branch)
        sljit_emit_op1(compiler, SLJIT_MOV_P, state_base, 0, SLJIT_S0, 0);
    if (has_memory)
        sljit_emit_op1(compiler, SLJIT_MOV_P, cache_base, 0, SLJIT_MEM1(SLJIT_S0),
                       offsetof(CpuNativeState, data_cache));
    sljit_emit_op1(compiler, SLJIT_MOV_P, gpr_base, 0, SLJIT_MEM1(SLJIT_S0),
                   offsetof(CpuNativeState, registers));
    const auto read = [&](sljit_s32 destination, unsigned source) {
        if (source == 0U) {
            sljit_emit_op1(compiler, SLJIT_MOV, destination, 0, SLJIT_IMM, 0);
        } else if (cached[source] != 0) {
            if (!initialized[source]) {
                sljit_emit_op1(compiler, SLJIT_MOV, cached[source], 0, SLJIT_MEM1(gpr_base), source * 8U);
                initialized[source] = true;
            }
            sljit_emit_op1(compiler, SLJIT_MOV, destination, 0, cached[source], 0);
        } else {
            sljit_emit_op1(compiler, SLJIT_MOV, destination, 0, SLJIT_MEM1(gpr_base), source * 8U);
        }
    };
    const auto write = [&](unsigned destination, bool word_result = false) {
        if (destination == 0U)
            return;
        if (word_result)
            sljit_emit_op1(compiler, SLJIT_MOV_S32, SLJIT_R0, 0, SLJIT_R0, 0);
        if (cached[destination] != 0) {
            sljit_emit_op1(compiler, SLJIT_MOV, cached[destination], 0, SLJIT_R0, 0);
            initialized[destination] = dirty[destination] = true;
        } else {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(gpr_base), destination * 8U, SLJIT_R0, 0);
        }
    };
    const auto compare = [&](unsigned destination, bool signed_compare, sljit_s32 right, sljit_sw value) {
        sljit_emit_op2u(compiler, SLJIT_SUB | (signed_compare ? SLJIT_SET_SIG_LESS : SLJIT_SET_LESS),
                        SLJIT_R0, 0, right, value);
        sljit_emit_op_flags(compiler, SLJIT_MOV, SLJIT_R0, 0, signed_compare ? SLJIT_SIG_LESS : SLJIT_LESS);
        write(destination);
    };
    std::vector<sljit_jump*> failed_loads;
    const auto fail_if = [&](sljit_s32 condition, sljit_s32 left, sljit_sw left_value, sljit_s32 right,
                             sljit_sw right_value) {
        failed_loads.push_back(sljit_emit_cmp(compiler, condition, left, left_value, right, right_value));
    };
    unsigned store_count = 0;
    const auto emit_memory = [&](u32 instruction, unsigned rs, unsigned rt, sljit_sw immediate) {
        const unsigned opcode = instruction >> 26U;
        const bool store = opcode == 0x28U || opcode == 0x29U || opcode == 0x2bU || opcode == 0x3fU;
        const unsigned width = opcode == 0x20U || opcode == 0x24U || opcode == 0x28U   ? 1U
                               : opcode == 0x21U || opcode == 0x25U || opcode == 0x29U ? 2U
                               : opcode == 0x37U || opcode == 0x3fU                    ? 8U
                                                                                       : 4U;
        const bool signed_load = opcode == 0x20U || opcode == 0x21U || opcode == 0x23U;

        read(SLJIT_R0, rs);
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, immediate);
        fail_if(SLJIT_EQUAL, cache_base, 0, SLJIT_IMM, 0);
        if (width > 1U) {
            sljit_emit_op2(compiler, SLJIT_AND, SLJIT_R1, 0, SLJIT_R0, 0, SLJIT_IMM, width - 1U);
            fail_if(SLJIT_NOT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);
        }
        constexpr auto segment_mask = static_cast<sljit_sw>(0xffffffffe0000000ULL);
        constexpr auto kseg0 = static_cast<sljit_sw>(0xffffffff80000000ULL);
        sljit_emit_op2(compiler, SLJIT_AND, SLJIT_R1, 0, SLJIT_R0, 0, SLJIT_IMM, segment_mask);
        fail_if(SLJIT_NOT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, kseg0);

        sljit_emit_op2(compiler, SLJIT_AND, SLJIT_R1, 0, SLJIT_R0, 0, SLJIT_IMM, 0x1fffffffU);
        if (!store && width == 8U) {
            sljit_emit_op2(compiler, SLJIT_AND, SLJIT_R2, 0, SLJIT_R1, 0, SLJIT_IMM, 0x1c000000U);
            fail_if(SLJIT_NOT_EQUAL, SLJIT_R2, 0, SLJIT_IMM, 0);
        }
        sljit_emit_op2(compiler, SLJIT_AND, SLJIT_R2, 0, SLJIT_R1, 0, SLJIT_IMM, 0xfffff000U);
        sljit_emit_op2(compiler, SLJIT_LSHR, SLJIT_R3, 0, SLJIT_R1, 0, SLJIT_IMM, 4);
        sljit_emit_op2(compiler, SLJIT_AND, SLJIT_R3, 0, SLJIT_R3, 0, SLJIT_IMM, 511);
        sljit_emit_op2(compiler, SLJIT_MUL, SLJIT_R3, 0, SLJIT_R3, 0, SLJIT_IMM, sizeof(CacheLine<16>));
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0, cache_base, 0, SLJIT_R3, 0);

        sljit_emit_op1(compiler, SLJIT_MOV_U8, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R3),
                       offsetof(CacheLine<16>, valid));
        fail_if(SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
        sljit_emit_op1(compiler, SLJIT_MOV_U32, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R3),
                       offsetof(CacheLine<16>, tag));
        fail_if(SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_R2, 0);

        sljit_emit_op2(compiler, SLJIT_AND, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, 15);
        if (store) {
            const auto record = static_cast<sljit_sw>(offsetof(CpuNativeState, stores) +
                                                      store_count++ * sizeof(CpuNativeState::Store));
            sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_MEM1(state_base),
                           record + static_cast<sljit_sw>(offsetof(CpuNativeState::Store, line)), SLJIT_R3,
                           0);
            sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(state_base),
                           record + static_cast<sljit_sw>(offsetof(CpuNativeState::Store, offset)), SLJIT_R1,
                           0);
            sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(state_base),
                           record + static_cast<sljit_sw>(offsetof(CpuNativeState::Store, width)), SLJIT_IMM,
                           width);
            read(SLJIT_R0, rt);
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(state_base),
                           record + static_cast<sljit_sw>(offsetof(CpuNativeState::Store, value)), SLJIT_R0,
                           0);
            return;
        }
        if (rt == 0U)
            return;
        sljit_emit_op2(compiler, SLJIT_ADD, SLJIT_R3, 0, SLJIT_R3, 0, SLJIT_R1, 0);
        if (width == 1U) {
            sljit_emit_op1(compiler, signed_load ? SLJIT_MOV_S8 : SLJIT_MOV_U8, SLJIT_R0, 0,
                           SLJIT_MEM1(SLJIT_R3), 0);
        } else if (width == 2U) {
            sljit_emit_op1(compiler, SLJIT_MOV_U16, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R3), 0);
            sljit_emit_op1(compiler, signed_load ? SLJIT_REV_S16 : SLJIT_REV_U16, SLJIT_R0, 0, SLJIT_R0, 0);
        } else if (width == 4U) {
            sljit_emit_op1(compiler, SLJIT_MOV_U32, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R3), 0);
            sljit_emit_op1(compiler, signed_load ? SLJIT_REV_S32 : SLJIT_REV_U32, SLJIT_R0, 0, SLJIT_R0, 0);
        } else {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R3), 0);
            sljit_emit_op1(compiler, SLJIT_REV, SLJIT_R0, 0, SLJIT_R0, 0);
        }
        write(rt);
    };

    for (const u32 instruction : instructions) {
        const unsigned opcode = instruction >> 26U;
        const unsigned rs = (instruction >> 21U) & 31U;
        const unsigned rt = (instruction >> 16U) & 31U;
        const unsigned rd = (instruction >> 11U) & 31U;
        const unsigned sa = (instruction >> 6U) & 31U;
        const auto immediate = static_cast<sljit_sw>(std::bit_cast<s16>(static_cast<u16>(instruction)));

        if (terminal_branch(instruction)) {
            cpu_native::emit_terminal_control(compiler, instruction, state_base, read, write);
            continue;
        }

        if (opcode != 0) {
            if (opcode == 0x20U || opcode == 0x21U || opcode == 0x23U || opcode == 0x24U || opcode == 0x25U ||
                opcode == 0x27U || opcode == 0x37U || opcode == 0x28U || opcode == 0x29U || opcode == 0x2bU ||
                opcode == 0x3fU) {
                emit_memory(instruction, rs, rt, immediate);
                continue;
            }
            if (rt == 0U)
                continue;
            read(SLJIT_R0, rs);
            switch (opcode) {
            case 0x09:
            case 0x19:
                sljit_emit_op2(compiler, opcode == 0x09 ? SLJIT_ADD32 : SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0,
                               SLJIT_IMM, immediate);
                write(rt, opcode == 0x09);
                break;
            case 0x0a:
            case 0x0b:
                compare(rt, opcode == 0x0a, SLJIT_IMM, immediate);
                break;
            case 0x0c:
            case 0x0d:
            case 0x0e:
                sljit_emit_op2(compiler,
                               opcode == 0x0c   ? SLJIT_AND
                               : opcode == 0x0d ? SLJIT_OR
                                                : SLJIT_XOR,
                               SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, instruction & 0xffffU);
                write(rt);
                break;
            case 0x0f:
                sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM,
                               static_cast<sljit_sw>(std::bit_cast<s32>(instruction << 16U)));
                write(rt);
                break;
            default:
                return {};
            }
            continue;
        }

        const unsigned function = instruction & 63U;
        if (rd == 0U || function == 0x0fU)
            continue;
        switch (function) {
        case 0x00:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x06:
        case 0x07:
        case 0x14:
        case 0x16:
        case 0x17:
        case 0x38:
        case 0x3a:
        case 0x3b:
        case 0x3c:
        case 0x3e:
        case 0x3f: {
            const bool word_result = function <= 0x07;
            const bool variable =
                (function >= 0x04 && function <= 0x07) || (function >= 0x14 && function <= 0x17);
            const unsigned shift_kind = function & 3U;
            const sljit_s32 operation = shift_kind == 0   ? SLJIT_SHL
                                        : shift_kind == 2 ? SLJIT_LSHR
                                                          : SLJIT_ASHR;
            // Word arithmetic shifts observe the full source register before truncation.
            const sljit_s32 width = word_result && shift_kind != 3 ? SLJIT_32 : 0;
            read(SLJIT_R0, rt);
            if (variable) {
                read(SLJIT_R1, rs);
                sljit_emit_op2(compiler, SLJIT_AND, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM,
                               word_result ? 31 : 63);
            }
            const unsigned shift = sa + (function >= 0x3c ? 32U : 0U);
            sljit_emit_op2(compiler, operation | width, SLJIT_R0, 0, SLJIT_R0, 0,
                           variable ? SLJIT_R1 : SLJIT_IMM, variable ? 0 : shift);
            write(rd, word_result);
            break;
        }
        case 0x21:
        case 0x23:
        case 0x24:
        case 0x25:
        case 0x26:
        case 0x27:
        case 0x2d:
        case 0x2f: {
            read(SLJIT_R0, rs);
            read(SLJIT_R1, rt);
            const bool word_result = function == 0x21 || function == 0x23;
            const sljit_s32 operation = function == 0x21 || function == 0x2d   ? SLJIT_ADD
                                        : function == 0x23 || function == 0x2f ? SLJIT_SUB
                                        : function == 0x24                     ? SLJIT_AND
                                        : function == 0x26                     ? SLJIT_XOR
                                                                               : SLJIT_OR;
            sljit_emit_op2(compiler, operation | (word_result ? SLJIT_32 : 0), SLJIT_R0, 0, SLJIT_R0, 0,
                           SLJIT_R1, 0);
            if (function == 0x27)
                sljit_emit_op2(compiler, SLJIT_XOR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, -1);
            write(rd, word_result);
            break;
        }
        case 0x2a:
        case 0x2b:
            read(SLJIT_R0, rs);
            read(SLJIT_R1, rt);
            compare(rd, function == 0x2a, SLJIT_R1, 0);
            break;
        default:
            return {};
        }
    }

    for (unsigned reg = 1; reg < cached.size(); ++reg) {
        if (dirty[reg])
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(gpr_base), reg * 8U, cached[reg], 0);
    }
    if (store_count != 0U)
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(state_base), offsetof(CpuNativeState, store_count),
                       SLJIT_IMM, store_count);
    sljit_emit_return(compiler, SLJIT_MOV, SLJIT_IMM, 1);
    if (!failed_loads.empty()) {
        auto* failed = sljit_emit_label(compiler);
        for (auto* jump : failed_loads)
            sljit_set_label(jump, failed);
        sljit_emit_return(compiler, SLJIT_MOV, SLJIT_IMM, 0);
    }
    if (sljit_get_compiler_error(compiler) != SLJIT_SUCCESS)
        return {};
    const u32 last = instructions.back();
    const bool links = (last >> 26U) == 3U || ((last >> 26U) == 0U && (last & 63U) == 9U);
    auto result = std::shared_ptr<CpuNativeCode>(new CpuNativeCode(nullptr, links));
    result->code_ = sljit_generate_code(compiler, 0, nullptr);
    return result->code_ == nullptr ? std::shared_ptr<const CpuNativeCode>{} : result;
#endif
}

} // namespace cupid
