#include "cupid/rsp/native.hpp"

#include "cupid/rsp.hpp"
#include "native/plan.hpp"
#include "native/vector.hpp"
#include "vector_operations.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <type_traits>
#include <utility>

#if defined(CUPID_RSP_NATIVE)
#include <sljitLir.h>
#endif

namespace cupid {
static_assert(std::is_standard_layout_v<RspNativeState>);

bool RspNativeCode::available() {
#if defined(CUPID_RSP_NATIVE)
    return true;
#else
    return false;
#endif
}

RspNativeCode::~RspNativeCode() {
#if defined(CUPID_RSP_NATIVE)
    if (code_ != nullptr)
        sljit_free_code(code_, nullptr);
#endif
}

// Generated functions have no compiler-emitted UBSan type signature before the
// entry address. Other sanitizer checks remain enabled in the C++ helpers.
#if defined(__clang__)
__attribute__((no_sanitize("function")))
#endif
void RspNativeCode::execute(RspNativeState& state) const {
    static_assert(sizeof(Rsp::Vector) == 16U);
    static_assert(std::is_standard_layout_v<Rsp::Accumulator>);
    static_assert(offsetof(Rsp::Accumulator, middle) == 16U);
    static_assert(offsetof(Rsp::Accumulator, high) == 32U);
    if (inline_vectors_) {
        state.vectors = state.rsp->vr_[0].lane.data();
        state.accumulator = state.rsp->accumulator_.low.data();
    }
    using Entry = void (*)(RspNativeState*);
    reinterpret_cast<Entry>(code_)(&state);
}

template <unsigned Function, unsigned Element> void RspNativeCode::vector(Rsp* rsp, u32 word) {
    if (!rsp->execute_vector_op_known<Function, Element>(word))
        rsp->execute_vector_op_scalar(word);
}

void RspNativeCode::cop2(Rsp* rsp, u32 word) {
    rsp->execute_cop2(word);
}

void RspNativeCode::load_vector(Rsp* rsp, u32 word) {
    rsp->execute_vector_load(word);
}

void RspNativeCode::store_vector(Rsp* rsp, u32 word) {
    rsp->execute_vector_store(word);
}

void RspNativeCode::load_wrapped(Rsp* rsp, u32 word) {
    const unsigned base = (word >> 21U) & 31U;
    const unsigned target = (word >> 16U) & 31U;
    const u32 address =
        rsp->gpr_[base] + static_cast<u32>(static_cast<s32>(std::bit_cast<s16>(static_cast<u16>(word))));
    switch (word >> 26U) {
    case 0x21:
        rsp->write_gpr(target,
                       static_cast<u32>(static_cast<s32>(std::bit_cast<s16>(rsp->dmem_read16(address)))));
        break;
    case 0x23:
    case 0x27:
        rsp->write_gpr(target, rsp->dmem_read32(address));
        break;
    case 0x25:
        rsp->write_gpr(target, rsp->dmem_read16(address));
        break;
    default:
        break;
    }
}

void RspNativeCode::store_byte(Rsp* rsp, u32 address, u32 value) {
    rsp->dmem_write8(address, static_cast<u8>(value));
}

void RspNativeCode::store_halfword(Rsp* rsp, u32 address, u32 value) {
    rsp->dmem_write16(address, static_cast<u16>(value));
}

void RspNativeCode::store_word(Rsp* rsp, u32 address, u32 value) {
    rsp->dmem_write32(address, value);
}

std::shared_ptr<const RspNativeCode>
RspNativeCode::compile(std::span<const RspNativeInstruction> instructions) {
#if !defined(CUPID_RSP_NATIVE)
    (void)instructions;
    return {};
#else
    if (instructions.empty() || instructions.size() > 16U)
        return {};
    const std::unique_ptr<sljit_compiler, decltype(&sljit_free_compiler)> owner(
        sljit_create_compiler(nullptr), sljit_free_compiler);
    auto* compiler = owner.get();
    if (compiler == nullptr)
        return {};

    const bool inline_vectors = std::any_of(instructions.begin(), instructions.end(), [](auto instruction) {
        return instruction.operation == RspPipeline::Operation::Cop2 &&
               (instruction.word & (1U << 25U)) != 0U && rsp_native::supports_vector(instruction.word & 63U);
    });
    const auto vector_plan = rsp_native::plan_vectors(instructions);
    rsp_native::AccumulatorCache accumulator_cache{vector_plan.cache_accumulator};
    sljit_emit_enter(compiler, 0, SLJIT_ARGS1V(P),
                     4 | SLJIT_ENTER_VECTOR(inline_vectors ? accumulator_cache.enabled ? 8 : 5 : 0),
                     inline_vectors ? 5 : 3, 0);
    sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_S1, 0, SLJIT_MEM1(SLJIT_S0),
                   offsetof(RspNativeState, scalar));
    sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_S2, 0, SLJIT_MEM1(SLJIT_S0), offsetof(RspNativeState, dmem));
    if (inline_vectors) {
        sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_S3, 0, SLJIT_MEM1(SLJIT_S0),
                       offsetof(RspNativeState, vectors));
        sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_S4, 0, SLJIT_MEM1(SLJIT_S0),
                       offsetof(RspNativeState, accumulator));
    }
    sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_S0, 0, SLJIT_MEM1(SLJIT_S0), offsetof(RspNativeState, rsp));

    const auto read = [&](sljit_s32 target, unsigned source) {
        if (source == 0U)
            sljit_emit_op1(compiler, SLJIT_MOV32, target, 0, SLJIT_IMM, 0);
        else
            sljit_emit_op1(compiler, SLJIT_MOV_U32, target, 0, SLJIT_MEM1(SLJIT_S1), source * 4U);
    };
    const auto write = [&](unsigned target, sljit_s32 source = SLJIT_R0) {
        if (target != 0U)
            sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S1), target * 4U, source, 0);
    };
    bool comparison_bias_live = false;
    const auto call = [&](void (*helper)(Rsp*, u32), u32 word) {
        rsp_native::flush_accumulator(compiler, accumulator_cache);
        comparison_bias_live = false;
        sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R1, 0, SLJIT_IMM, static_cast<sljit_sw>(word));
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(P, 32), SLJIT_IMM,
                         reinterpret_cast<sljit_sw>(helper));
    };
    static constexpr auto vectors = []<std::size_t... Indices>(std::index_sequence<Indices...>) {
        return std::array<void (*)(Rsp*, u32), sizeof...(Indices)>{
            &RspNativeCode::vector<static_cast<unsigned>(Indices & 63U),
                                   static_cast<unsigned>(Indices >> 6U)>...};
    }(std::make_index_sequence<64U * 16U>{});

    using Op = RspPipeline::Operation;
    for (std::size_t instruction_index = 0; instruction_index < instructions.size(); ++instruction_index) {
        const auto instruction = instructions[instruction_index];
        const u32 word = instruction.word;
        const unsigned rs = (word >> 21U) & 31U;
        const unsigned rt = (word >> 16U) & 31U;
        const unsigned rd = (word >> 11U) & 31U;
        const unsigned sa = (word >> 6U) & 31U;
        const auto immediate = static_cast<sljit_sw>(std::bit_cast<s16>(static_cast<u16>(word)));
        // Both arms of a wrapped-load branch must start with coherent memory.
        // A slow-path-only flush would clear dirty bookkeeping for the fast arm.
        if (instruction.operation == Op::Lh || instruction.operation == Op::Lhu ||
            instruction.operation == Op::Lw)
            rsp_native::flush_accumulator(compiler, accumulator_cache);
        switch (instruction.operation) {
        case Op::None:
            break;
        case Op::Sll:
        case Op::Srl:
        case Op::Sra: {
            if (rd == 0U)
                break;
            read(SLJIT_R0, rt);
            const sljit_s32 operation = instruction.operation == Op::Sll   ? SLJIT_SHL
                                        : instruction.operation == Op::Srl ? SLJIT_LSHR
                                                                           : SLJIT_ASHR;
            sljit_emit_op2(compiler, operation | SLJIT_32, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, sa);
            write(rd);
            break;
        }
        case Op::Sllv:
        case Op::Srlv:
        case Op::Srav:
        case Op::ReservedSpecial: {
            if (rd == 0U)
                break;
            read(SLJIT_R0, instruction.operation == Op::ReservedSpecial ? rs : rt);
            read(SLJIT_R1, rs);
            sljit_emit_op2(compiler, SLJIT_AND32, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, 31);
            const sljit_s32 operation = instruction.operation == Op::Sllv   ? SLJIT_SHL
                                        : instruction.operation == Op::Srav ? SLJIT_ASHR
                                                                            : SLJIT_LSHR;
            sljit_emit_op2(compiler, operation | SLJIT_32, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
            write(rd);
            break;
        }
        case Op::Addu:
        case Op::Subu:
        case Op::And:
        case Op::Or:
        case Op::Xor:
        case Op::Nor: {
            if (rd == 0U)
                break;
            read(SLJIT_R0, rs);
            read(SLJIT_R1, rt);
            const sljit_s32 operation = instruction.operation == Op::Addu   ? SLJIT_ADD
                                        : instruction.operation == Op::Subu ? SLJIT_SUB
                                        : instruction.operation == Op::And  ? SLJIT_AND
                                        : instruction.operation == Op::Xor  ? SLJIT_XOR
                                                                            : SLJIT_OR;
            sljit_emit_op2(compiler, operation | SLJIT_32, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
            if (instruction.operation == Op::Nor)
                sljit_emit_op2(compiler, SLJIT_XOR32, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, -1);
            write(rd);
            break;
        }
        case Op::Slt:
        case Op::Sltu:
        case Op::Slti:
        case Op::Sltiu: {
            const bool immediate_operand =
                instruction.operation == Op::Slti || instruction.operation == Op::Sltiu;
            const unsigned target = immediate_operand ? rt : rd;
            if (target == 0U)
                break;
            const bool signed_compare = instruction.operation == Op::Slt || instruction.operation == Op::Slti;
            read(SLJIT_R0, rs);
            if (!immediate_operand)
                read(SLJIT_R1, rt);
            sljit_emit_op2u(compiler, SLJIT_SUB32 | (signed_compare ? SLJIT_SET_SIG_LESS : SLJIT_SET_LESS),
                            SLJIT_R0, 0, immediate_operand ? SLJIT_IMM : SLJIT_R1,
                            immediate_operand ? immediate : 0);
            sljit_emit_op_flags(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S1), target * 4U,
                                signed_compare ? SLJIT_SIG_LESS : SLJIT_LESS);
            break;
        }
        case Op::Addiu:
        case Op::Andi:
        case Op::Ori:
        case Op::Xori: {
            if (rt == 0U)
                break;
            read(SLJIT_R0, rs);
            const sljit_s32 operation = instruction.operation == Op::Addiu  ? SLJIT_ADD
                                        : instruction.operation == Op::Andi ? SLJIT_AND
                                        : instruction.operation == Op::Ori  ? SLJIT_OR
                                                                            : SLJIT_XOR;
            sljit_emit_op2(compiler, operation | SLJIT_32, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM,
                           instruction.operation == Op::Addiu ? immediate
                                                              : static_cast<sljit_sw>(word & 0xffffU));
            write(rt);
            break;
        }
        case Op::Lui:
            if (rt != 0U)
                sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(SLJIT_S1), rt * 4U, SLJIT_IMM,
                               static_cast<sljit_sw>(word << 16U));
            break;
        case Op::Lb:
        case Op::Lbu:
        case Op::Lh:
        case Op::Lhu:
        case Op::Lw: {
            if (rt == 0U)
                break;
            const unsigned width = instruction.operation == Op::Lw                                       ? 4U
                                   : instruction.operation == Op::Lh || instruction.operation == Op::Lhu ? 2U
                                                                                                         : 1U;
            read(SLJIT_R0, rs);
            sljit_emit_op2(compiler, SLJIT_ADD32, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, immediate);
            sljit_emit_op2(compiler, SLJIT_AND32, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 4095);
            sljit_emit_op1(compiler, SLJIT_MOV_U32, SLJIT_R0, 0, SLJIT_R0, 0);
            sljit_jump* wrapped = nullptr;
            if (width != 1U)
                wrapped =
                    sljit_emit_cmp(compiler, SLJIT_GREATER | SLJIT_32, SLJIT_R0, 0, SLJIT_IMM, 4096U - width);
            const sljit_s32 load = width == 4U                       ? SLJIT_MOV_U32
                                   : width == 2U                     ? SLJIT_MOV_U16
                                   : instruction.operation == Op::Lb ? SLJIT_MOV32_S8
                                                                     : SLJIT_MOV_U8;
            sljit_emit_op1(compiler, load, SLJIT_R0, 0, SLJIT_MEM2(SLJIT_S2, SLJIT_R0), 0);
            if (width > 1U)
                sljit_emit_op1(compiler,
                               width == 4U                       ? SLJIT_REV32
                               : instruction.operation == Op::Lh ? SLJIT_REV32_S16
                                                                 : SLJIT_REV_U16,
                               SLJIT_R0, 0, SLJIT_R0, 0);
            write(rt);
            if (wrapped != nullptr) {
                auto* finished = sljit_emit_jump(compiler, SLJIT_JUMP);
                sljit_set_label(wrapped, sljit_emit_label(compiler));
                call(load_wrapped, word);
                sljit_set_label(finished, sljit_emit_label(compiler));
            }
            break;
        }
        case Op::Sb:
        case Op::Sh:
        case Op::Sw: {
            read(SLJIT_R1, rs);
            sljit_emit_op2(compiler, SLJIT_ADD32, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, immediate);
            read(SLJIT_R2, rt);
            sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
            const auto helper = instruction.operation == Op::Sb   ? store_byte
                                : instruction.operation == Op::Sh ? store_halfword
                                                                  : store_word;
            comparison_bias_live = false;
            rsp_native::flush_accumulator(compiler, accumulator_cache);
            sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(P, 32, 32), SLJIT_IMM,
                             reinterpret_cast<sljit_sw>(helper));
            break;
        }
        case Op::Cop2: {
            const unsigned function = word & 63U;
            const unsigned element = (word >> 21U) & 15U;
            if ((word & (1U << 25U)) != 0U && rsp_native::supports_vector(function))
                rsp_native::emit_vector(compiler, word, comparison_bias_live,
                                        vector_plan.destination_live[instruction_index], accumulator_cache);
            else
                call((word & (1U << 25U)) != 0U ? vectors[element * 64U + function] : cop2, word);
            break;
        }
        case Op::VectorLoad:
            call(load_vector, word);
            break;
        case Op::VectorStore:
            call(store_vector, word);
            break;
        default:
            return {};
        }
    }
    rsp_native::flush_accumulator(compiler, accumulator_cache);
    sljit_emit_return_void(compiler);
    if (sljit_get_compiler_error(compiler) != SLJIT_SUCCESS)
        return {};
    auto result = std::shared_ptr<RspNativeCode>(new RspNativeCode(nullptr, inline_vectors));
    result->code_ = sljit_generate_code(compiler, 0, nullptr);
    if (result->code_ == nullptr)
        return {};
    return result;
#endif
}

} // namespace cupid
