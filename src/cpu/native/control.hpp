#pragma once

#include "cupid/cpu/native.hpp"

#include <cstddef>

#if defined(CUPID_CPU_NATIVE)
#include <sljitLir.h>

namespace cupid::cpu_native {

template <typename Read, typename Write>
void emit_terminal_control(sljit_compiler* compiler, u32 instruction, sljit_s32 state_base, const Read& read,
                           const Write& write) {
    const unsigned opcode = instruction >> 26U;
    const unsigned rs = (instruction >> 21U) & 31U;
    const unsigned rt = (instruction >> 16U) & 31U;
    const unsigned rd = (instruction >> 11U) & 31U;
    if (opcode == 0U || opcode == 2U || opcode == 3U) {
        if (opcode == 0U)
            read(SLJIT_R0, rs);
        else
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, (instruction & 0x03ffffffU) << 2U);
        sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_MEM1(state_base), offsetof(CpuNativeState, branch_target),
                       SLJIT_R0, 0);
        if (opcode == 3U || (opcode == 0U && (instruction & 63U) == 9U)) {
            sljit_emit_op1(compiler, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(state_base),
                           offsetof(CpuNativeState, branch_link));
            write(opcode == 3U ? 31U : rd);
        }
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(state_base), offsetof(CpuNativeState, branch_taken),
                       SLJIT_IMM, 1);
        return;
    }
    read(SLJIT_R0, rs);
    const bool pair = opcode == 4U || opcode == 5U;
    if (pair)
        read(SLJIT_R1, rt);
    const sljit_s32 condition = opcode == 4U   ? SLJIT_EQUAL
                                : opcode == 5U ? SLJIT_NOT_EQUAL
                                : opcode == 6U ? SLJIT_SIG_LESS_EQUAL
                                : opcode == 7U ? SLJIT_SIG_GREATER
                                : rt == 0U     ? SLJIT_SIG_LESS
                                               : SLJIT_SIG_GREATER_EQUAL;
    const sljit_s32 flags = pair                           ? SLJIT_SET_Z
                            : opcode == 6U || opcode == 7U ? SLJIT_SET_SIG_GREATER
                                                           : SLJIT_SET_SIG_LESS;
    sljit_emit_op2u(compiler, SLJIT_SUB | flags, SLJIT_R0, 0, pair ? SLJIT_R1 : SLJIT_IMM, 0);
    sljit_emit_op_flags(compiler, SLJIT_MOV, SLJIT_R0, 0, condition);
    sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_MEM1(state_base), offsetof(CpuNativeState, branch_taken),
                   SLJIT_R0, 0);
}

} // namespace cupid::cpu_native
#endif
