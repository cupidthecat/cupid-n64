#include "core/cpu/recompiler/compiler.hpp"
#include <algorithm>

namespace cupid::n64 {

CpuCompiler::Emitter::Emitter(Cpu &cpu, Impl::Block &block, std::uint64_t pc,
                              std::uint32_t physical, bool wide)
    : cpu(cpu), block(block), compiler(sljit_create_compiler(nullptr)), start_pc(pc), pc(pc),
      physical(physical), wide(wide) {}

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

sljit_sw CpuCompiler::Emitter::guard(Cpu *cpu, std::uint32_t physical,
                                     const Impl::Block::InstructionView *view) {
  auto &line = cpu->icache_[(cpu->state_.pc >> 5) & 511];
  if (line.hit(physical))
    return 1;
  if (!cpu->fill(line, physical, static_cast<std::uint32_t>(cpu->state_.pc) & 0xfe0, true))
    return 0;
  const unsigned reverse = cpu->little_endian() ? 1 : 0;
  for (unsigned n = 0; n < view->count; ++n)
    if (view->words[n] != line.words[(((physical >> 2) + n) ^ reverse) & 7]) {
      helper(cpu, line.words[((physical >> 2) ^ reverse) & 7], 0);
      return 0;
    }
  return 1;
}

sljit_sw CpuCompiler::Emitter::helper(Cpu *cpu, std::uint32_t instruction, sljit_uw clocks) {
  cpu->advance_clocks(clocks);
  const auto pc = cpu->state_.pc;
  cpu->begin_instruction();
  cpu->decode(instruction);
  const auto self_jump = (2u << 26) | static_cast<std::uint32_t>((pc >> 2) & 0x03ffffff);
  if (instruction == 0x1000ffff || instruction == self_jump)
    cpu->advance_clocks(126);
  const auto exit = unsigned(cpu->block_exit_) | (cpu->state_.pc != pc ? 2u : 0u);
  cpu->end_instruction();
  return exit;
}

void CpuCompiler::Emitter::cache_guard(std::uint32_t address) {
  commit_pipeline();
  advance(cycles);
  cycles = 0;
  const auto &line = cpu.icache_[(pc >> 5) & 511];
  op1(SLJIT_MOV_U8, reg(SLJIT_R0), field(&line.valid));
  const auto invalid = sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
  const auto hit = sljit_emit_cmp(compiler, SLJIT_EQUAL | SLJIT_32, field(&line.tag).type,
                                  field(&line.tag).value, SLJIT_IMM, address & ~0xfffu);
  sljit_set_label(invalid, sljit_emit_label(compiler));
  const auto index = static_cast<unsigned>((pc - start_pc) >> 2);
  auto &view = block.views.emplace_back();
  view.count = static_cast<unsigned>(
      std::min<std::size_t>(8 - ((address >> 2) & 7), block.words.size() - index));
  std::copy_n(block.words.begin() + index, view.count, view.words.begin());
  op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S2));
  op1(SLJIT_MOV32, reg(SLJIT_R1), imm(address));
  op1(SLJIT_MOV, reg(SLJIT_R2), imm(reinterpret_cast<std::uintptr_t>(&view)));
  sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, P, 32, P), SLJIT_IMM,
                   SLJIT_FUNC_ADDR(guard));
  return_if(SLJIT_EQUAL, reg(SLJIT_R0), imm(0), 0);
  sljit_set_label(hit, sljit_emit_label(compiler));
}

void CpuCompiler::Emitter::execute(std::uint32_t instruction, bool defer_exit) {
  commit_pipeline();
  op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S2));
  op1(SLJIT_MOV32, reg(SLJIT_R1), imm(instruction));
  op1(SLJIT_MOV, reg(SLJIT_R2), imm(cycles));
  sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, P, 32, W), SLJIT_IMM,
                   SLJIT_FUNC_ADDR(helper));
  cycles = 0;
  if (defer_exit)
    op1(SLJIT_MOV, reg(SLJIT_S3), reg(SLJIT_R0));
  else
    return_if(SLJIT_NOT_EQUAL, reg(SLJIT_R0), imm(0), 0);
}

bool CpuCompiler::Emitter::compile() {
  if (!compiler)
    return false;
  // Generated code keeps pointers to the fetch views.
  block.views.reserve(block.words.size());
  sljit_emit_enter(compiler, 0, SLJIT_ARGS1V(P), 4, 4, 0);
  op1(SLJIT_MOV, reg(SLJIT_S1), imm(reinterpret_cast<std::uintptr_t>(&cpu.state_)));
  op1(SLJIT_MOV, reg(SLJIT_S2), imm(reinterpret_cast<std::uintptr_t>(&cpu)));
  plan_entries();
  for (auto index : block.entries)
    internal_jumps.emplace_back(sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_MEM1(SLJIT_S1),
                                               offsetof(CpuState, pc), SLJIT_IMM,
                                               static_cast<sljit_sw>(start_pc + index * 4)),
                                index);
  bool previous_branch = false;
  bool conditional_delay = false;
  for (unsigned n = 0; n < block.words.size(); ++n) {
    const auto instruction = block.words[n];
    const auto info = block_instruction(instruction);
    if (internal_entries[n]) {
      commit_pipeline();
      advance(cycles);
      cycles = 0;
    }
    instruction_labels[n] = sljit_emit_label(compiler);
    const auto target = previous_branch ? internal_target(n - 1) : std::nullopt;
    const bool defer_exit = target.has_value();
    cycles += 2;
    if (!n || !(pc & 31) || internal_entries[n])
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
      const bool full = !n || previous_branch || internal_entries[n];
      if (full)
        begin();
      integer(instruction);
      if (full)
        end(defer_exit);
      else
        pipeline_dirty = true;
    } else if (!branch(instruction) &&
               !memory(instruction, !n || previous_branch || internal_entries[n], defer_exit)) {
      execute(instruction, defer_exit);
    }
    pc += 4;
    if (target)
      dispatch_internal(*target);
    if ((!info.branch && info.terminal) || opcode == 47) {
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
  memory_slow_paths();
  for (const auto &[jump, target] : internal_jumps)
    sljit_set_label(jump, instruction_labels[target]);
  block.code = sljit_generate_code(compiler, 0, nullptr);
  block.bytes = static_cast<std::size_t>(sljit_get_generated_code_size(compiler));
  return block.code != nullptr;
}

} // namespace cupid::n64
