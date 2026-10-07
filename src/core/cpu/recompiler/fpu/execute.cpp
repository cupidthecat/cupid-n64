#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

bool CpuCompiler::Emitter::floating(std::uint32_t instruction, bool full, bool defer_exit) {
  if ((instruction >> 26) != 17 || !(cpu.control_[Status] & 0x20000000))
    return false;
  const auto format = (instruction >> 21) & 31;
  const auto operation = instruction & 63;
  const bool real = format == 16 || format == 17;
  const bool transfer =
      format == 0 || format == 1 || format == 4 || format == 5 || (real && operation == 6);
  if (transfer) {
    if (full)
      begin();
    floating_transfer(instruction);
    if (full)
      end(defer_exit);
    else {
      cycles += 2;
      pipeline_dirty = true;
    }
    return true;
  }
  const bool arithmetic = real && operation <= 7;
  const bool compare = real && operation >= 0x30;
  const bool convert =
      (real && ((operation >= 8 && operation <= 15) || operation == 0x24 || operation == 0x25 ||
                (format == 16 && operation == 0x21) || (format == 17 && operation == 0x20))) ||
      ((format == 20 || format == 21) && (operation == 0x20 || operation == 0x21));
  if (!floating_host() || (!arithmetic && !compare && !convert))
    return false;
  commit_pipeline();
  advance(cycles);
  cycles = 0;
  SlowPath path{{}, nullptr, instruction, 0, defer_exit};
  if (arithmetic)
    floating_arithmetic(path);
  else if (compare)
    floating_compare(path);
  else
    floating_convert(path);
  // All fault checks precede the result and pipeline updates.
  if (full)
    begin();
  if (full)
    end(defer_exit);
  else {
    cycles += 2;
    pipeline_dirty = true;
  }
  path.resume = sljit_emit_label(compiler);
  slow_paths.push_back(std::move(path));
  return true;
}

void CpuCompiler::Emitter::floating_input(SlowPath &path, sljit_s32 source, bool dual,
                                          bool subnormal) {
  const auto narrow = dual ? 0 : SLJIT_32;
  op2(SLJIT_SHL | narrow, reg(SLJIT_R2), reg(source), imm(1));
  path.enter.push_back(sljit_emit_cmp(compiler, SLJIT_GREATER | narrow, SLJIT_R2, 0, SLJIT_IMM,
                                      dual ? sljit_sw(0xffe0000000000000ull) : 0xff000000));
  if (subnormal) {
    op2(SLJIT_SUB | narrow, reg(SLJIT_R3), reg(SLJIT_R2), imm(1));
    path.enter.push_back(sljit_emit_cmp(compiler, SLJIT_LESS | narrow, SLJIT_R3, 0, SLJIT_IMM,
                                        dual ? 0x001fffffffffffffull : 0x00ffffff));
  }
}

void CpuCompiler::Emitter::floating_flags(SlowPath &path, bool dual, bool flush) {
  const auto control = cpu.state_.fcr31;
  const unsigned enabled = (control >> 7) & 31;
  const unsigned traps = ((enabled & 1) << 5) | ((enabled & 2) << 3) | ((enabled & 4) << 1) |
                         ((enabled & 8) >> 1) | ((enabled & 16) >> 4);
  const unsigned fallback = 3 | traps | ((control & 0x01000000) ? 0 : 16);
  op2(SLJIT_AND | SLJIT_32, reg(SLJIT_R0), reg(SLJIT_R3), imm(fallback));
  path.enter.push_back(sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0));
  if (flush && (control & 0x01000000) && (control & 2)) {
    op2(SLJIT_AND | SLJIT_32, reg(SLJIT_R0), reg(SLJIT_R3), imm(16));
    const auto normal = sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
    const auto sign = dual ? 0x8000000000000000ull : 0x80000000ull;
    const auto minimum = dual ? 0x0010000000000000ull : 0x00800000ull;
    op2(SLJIT_AND, reg(SLJIT_R0), reg(SLJIT_R1), imm(sign));
    const auto zero = sljit_emit_cmp(compiler, (control & 1) ? SLJIT_EQUAL : SLJIT_NOT_EQUAL,
                                     SLJIT_R0, 0, SLJIT_IMM, 0);
    op2(SLJIT_OR, reg(SLJIT_R1), reg(SLJIT_R0), imm(minimum));
    sljit_set_label(zero, sljit_emit_label(compiler));
    sljit_set_label(normal, sljit_emit_label(compiler));
  }
  op2(SLJIT_AND | SLJIT_32, reg(SLJIT_R0), reg(SLJIT_R3), imm(32));
  op2(SLJIT_LSHR | SLJIT_32, reg(SLJIT_R0), reg(SLJIT_R0), imm(5));
  for (const auto &[mask, shift] : {std::pair{16u, 3u}, {8u, 1u}, {4u, 0u}}) {
    op2(SLJIT_AND | SLJIT_32, reg(SLJIT_R2), reg(SLJIT_R3), imm(mask));
    op2((shift ? SLJIT_LSHR : SLJIT_SHL) | SLJIT_32, reg(SLJIT_R2), reg(SLJIT_R2),
        imm(shift ? shift : 1));
    op2(SLJIT_OR | SLJIT_32, reg(SLJIT_R0), reg(SLJIT_R0), reg(SLJIT_R2));
  }
  op2(SLJIT_SHL | SLJIT_32, reg(SLJIT_R2), reg(SLJIT_R0), imm(12));
  op2(SLJIT_SHL | SLJIT_32, reg(SLJIT_R0), reg(SLJIT_R0), imm(2));
  op2(SLJIT_OR | SLJIT_32, reg(SLJIT_R0), reg(SLJIT_R0), reg(SLJIT_R2));
  const auto csr = state(offsetof(CpuState, fcr31));
  op2(SLJIT_AND | SLJIT_32, reg(SLJIT_R2), csr, imm(~0x0003f000u));
  op2(SLJIT_OR | SLJIT_32, csr, reg(SLJIT_R2), reg(SLJIT_R0));
}

} // namespace cupid::n64
