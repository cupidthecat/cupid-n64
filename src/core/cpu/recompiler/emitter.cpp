#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

CpuCompiler::Emitter::Emitter(Cpu &cpu, Impl::Block &block, std::uint64_t pc,
                              std::uint32_t physical, bool wide)
    : cpu(cpu), block(block), compiler(sljit_create_compiler(nullptr)), pc(pc), physical(physical),
      wide(wide) {}

CpuCompiler::Emitter::~Emitter() {
  sljit_free_compiler(compiler);
}

void CpuCompiler::Emitter::op1(sljit_s32 op, Operand dest, Operand source) {
  sljit_emit_op1(compiler, op, dest.type, dest.value, source.type, source.value);
}

void CpuCompiler::Emitter::op2(sljit_s32 op, Operand dest, Operand left, Operand right) {
  sljit_emit_op2(compiler, op, dest.type, dest.value, left.type, left.value, right.type,
                 right.value);
}

void CpuCompiler::Emitter::compare(Operand dest, Operand left, Operand right, bool is_signed) {
  const auto condition = is_signed ? SLJIT_SIG_LESS : SLJIT_LESS;
  sljit_emit_op2u(compiler, SLJIT_SUB | SLJIT_SET(condition), left.type, left.value, right.type,
                  right.value);
  sljit_emit_op_flags(compiler, SLJIT_MOV, dest.type, dest.value, condition);
}

void CpuCompiler::Emitter::store(unsigned dest, Operand source, bool word) {
  if (!dest)
    return;
  if (word) {
    op1(SLJIT_MOV_S32, reg(SLJIT_R0), source);
    source = reg(SLJIT_R0);
  }
  op1(SLJIT_MOV, gpr(dest), source);
}

sljit_sw CpuCompiler::Emitter::guard(Cpu *cpu, std::uint32_t physical, std::uint32_t index,
                                     sljit_uw clocks) {
  cpu->advance_clocks(clocks);
  auto &line = cpu->icache_[(index >> 5) & 511];
  return line.hit(physical) || cpu->fill(line, physical, index & 0xfe0, true);
}

sljit_sw CpuCompiler::Emitter::helper(Cpu *cpu, std::uint32_t instruction, sljit_uw clocks) {
  cpu->advance_clocks(clocks);
  const auto pc = cpu->state_.pc;
  cpu->begin_instruction();
  cpu->decode(instruction);
  const auto self_jump = (2u << 26) | static_cast<std::uint32_t>((pc >> 2) & 0x03ffffff);
  if (instruction == 0x1000ffff || instruction == self_jump)
    cpu->advance_clocks(126);
  const auto exit = cpu->block_exit_;
  cpu->end_instruction();
  return exit;
}

sljit_sw CpuCompiler::Emitter::prepare(Cpu *cpu, sljit_uw clocks) {
  cpu->advance_clocks(clocks);
  cpu->begin_instruction();
  return cpu->block_exit_;
}

void CpuCompiler::Emitter::step(Cpu *cpu, sljit_uw clocks) {
  cpu->advance_clocks(clocks);
}

void CpuCompiler::Emitter::commit_pipeline() {
  if (!pipeline_dirty)
    return;
  op1(SLJIT_MOV, state(offsetof(CpuState, pc)), imm(pc));
  op1(SLJIT_MOV, field(&cpu.pipeline_pc_), imm(pc));
  op1(SLJIT_MOV, field(&cpu.next_pc_), imm(pc + 4));
  pipeline_dirty = false;
}

void CpuCompiler::Emitter::advance(unsigned clocks) {
  if (!clocks)
    return;
  op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S2));
  op1(SLJIT_MOV, reg(SLJIT_R1), imm(clocks));
  sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(P, W), SLJIT_IMM, SLJIT_FUNC_ADDR(step));
}

void CpuCompiler::Emitter::return_now(unsigned clocks) {
  advance(clocks);
  sljit_emit_return_void(compiler);
}

void CpuCompiler::Emitter::return_if(sljit_s32 condition, Operand left, Operand right,
                                     unsigned clocks) {
  const auto skip =
      sljit_emit_cmp(compiler, condition ^ 1, left.type, left.value, right.type, right.value);
  return_now(clocks);
  sljit_set_label(skip, sljit_emit_label(compiler));
}

void CpuCompiler::Emitter::cache_guard(std::uint32_t address) {
  commit_pipeline();
  op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S2));
  op1(SLJIT_MOV32, reg(SLJIT_R1), imm(address));
  op1(SLJIT_MOV32, reg(SLJIT_R2), imm(static_cast<std::uint32_t>(pc)));
  op1(SLJIT_MOV, reg(SLJIT_R3), imm(cycles));
  sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4(W, P, 32, 32, W), SLJIT_IMM,
                   SLJIT_FUNC_ADDR(guard));
  cycles = 0;
  return_if(SLJIT_EQUAL, reg(SLJIT_R0), imm(0), 0);
}

void CpuCompiler::Emitter::execute(std::uint32_t instruction) {
  commit_pipeline();
  op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S2));
  op1(SLJIT_MOV32, reg(SLJIT_R1), imm(instruction));
  op1(SLJIT_MOV, reg(SLJIT_R2), imm(cycles));
  sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, P, 32, W), SLJIT_IMM,
                   SLJIT_FUNC_ADDR(helper));
  cycles = 0;
  return_if(SLJIT_NOT_EQUAL, reg(SLJIT_R0), imm(0), 0);
}

void CpuCompiler::Emitter::begin() {
  commit_pipeline();
  op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S2));
  op1(SLJIT_MOV, reg(SLJIT_R1), imm(cycles));
  sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(W, P, W), SLJIT_IMM, SLJIT_FUNC_ADDR(prepare));
  op1(SLJIT_MOV, reg(SLJIT_S3), reg(SLJIT_R0));
  cycles = 0;
}

void CpuCompiler::Emitter::end() {
  op1(SLJIT_MOV, gpr(0), imm(0));
  op1(SLJIT_MOV, state(offsetof(CpuState, pc)), field(&cpu.pipeline_pc_));
  op1(SLJIT_MOV_U8, field(&cpu.delay_slot_), imm(0));
  op1(SLJIT_MOV_U8, field(&cpu.block_exit_), imm(0));
  return_if(SLJIT_NOT_EQUAL, reg(SLJIT_S3), imm(0), cycles);
}

bool CpuCompiler::Emitter::compile() {
  if (!compiler)
    return false;
  sljit_emit_enter(compiler, 0, SLJIT_ARGS1V(P), 4, 4, 0);
  op1(SLJIT_MOV, reg(SLJIT_S1), imm(reinterpret_cast<std::uintptr_t>(&cpu.state_)));
  op1(SLJIT_MOV, reg(SLJIT_S2), imm(reinterpret_cast<std::uintptr_t>(&cpu)));
  bool previous_branch = false;
  bool conditional_delay = false;
  for (unsigned n = 0; n < block.words.size(); ++n) {
    const auto instruction = block.words[n];
    const auto info = block_instruction(instruction);
    cycles += 2;
    if (!n || !(pc & 31))
      cache_guard(physical + n * 4);
    const auto opcode = instruction >> 26;
    const auto function = instruction & 63;
    const bool native =
        (opcode == 0 &&
         (function == 0 || function == 2 || function == 3 || function == 4 || function == 6 ||
          function == 7 || function == 15 || (function >= 16 && function <= 23 && function != 21) ||
          function == 33 || function == 35 || (function >= 36 && function <= 39) ||
          function == 42 || function == 43 || function == 45 || function == 47 || function == 56 ||
          function == 58 || function == 59 || function == 60 || function == 62 ||
          function == 63)) ||
        opcode == 9 || (opcode >= 10 && opcode <= 15) || opcode == 25;
    const bool requires_wide =
        opcode == 25 || (opcode == 0 && (function == 20 || function == 22 || function == 23 ||
                                         function == 45 || function == 47 || function >= 56));
    if (native && (!requires_wide || wide)) {
      const bool full = !n || previous_branch;
      if (full)
        begin();
      integer(instruction);
      if (full)
        end();
      else
        pipeline_dirty = true;
    } else {
      execute(instruction);
    }
    pc += 4;
    if (!info.branch && info.terminal) {
      commit_pipeline();
      return_now(cycles);
      break;
    }
    if (conditional_delay) {
      commit_pipeline();
      advance(cycles);
      cycles = 0;
      return_if(SLJIT_GREATER_EQUAL, state(offsetof(CpuState, clocks)), {SLJIT_MEM1(SLJIT_S0)}, 0);
    }
    previous_branch = info.branch;
    conditional_delay =
        info.branch && !info.stop_after_delay && opcode != 3 && !(opcode == 0 && function == 9);
  }
  commit_pipeline();
  return_now(cycles);
  block.code = sljit_generate_code(compiler, 0, nullptr);
  block.bytes = static_cast<std::size_t>(sljit_get_generated_code_size(compiler));
  return block.code != nullptr;
}

} // namespace cupid::n64
