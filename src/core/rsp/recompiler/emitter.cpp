#include "core/rsp/recompiler/compiler.hpp"

namespace cupid::n64 {

RspCompiler::Emitter::Emitter(Rsp &rsp, Impl::Block &block)
    : rsp(rsp), block(block), compiler(sljit_create_compiler(nullptr)), pipeline(rsp.pipeline_),
      start(rsp.pc_) {}

RspCompiler::Emitter::~Emitter() {
  sljit_free_compiler(compiler);
}

void RspCompiler::Emitter::op1(sljit_s32 op, Operand dest, Operand source) {
  sljit_emit_op1(compiler, op, dest.type, dest.value, source.type, source.value);
}

void RspCompiler::Emitter::op2(sljit_s32 op, Operand dest, Operand left, Operand right) {
  sljit_emit_op2(compiler, op, dest.type, dest.value, left.type, left.value, right.type,
                 right.value);
}

void RspCompiler::Emitter::store(unsigned dest, Operand source) {
  if (dest)
    op1(SLJIT_MOV32, gpr(dest), source);
}

void RspCompiler::Emitter::compare(unsigned dest, Operand left, Operand right, bool is_signed) {
  const auto condition = is_signed ? SLJIT_SIG_LESS : SLJIT_LESS;
  sljit_emit_op2u(compiler, SLJIT_SUB32 | SLJIT_SET(condition), left.type, left.value, right.type,
                  right.value);
  sljit_emit_op_flags(compiler, SLJIT_MOV32, SLJIT_R0, 0, condition);
  store(dest, reg(SLJIT_R0));
}

std::uint32_t RspCompiler::Emitter::word(unsigned index) {
  while (index >= block.words.size())
    block.words.push_back(static_cast<std::uint32_t>(
        rsp.read_local(0x1000 | ((start + block.words.size() * 4) & 0xfff), 4)));
  return block.words[index];
}

void RspCompiler::Emitter::helper(Rsp *rsp, std::uint32_t instruction, std::uint32_t pc) {
  rsp->pc_ = pc;
  rsp->decode(instruction);
  rsp->state_.gpr[0] = 0;
}

void RspCompiler::Emitter::vector_helper(Rsp *rsp, std::uint32_t instruction) {
  rsp->vector_execute(instruction);
  rsp->state_.gpr[0] = 0;
}

void RspCompiler::Emitter::branch_helper(Rsp *rsp, std::uint32_t instruction, std::uint32_t pc) {
  rsp->pc_ = pc;
  rsp->next_pc_ = (pc + 4) & 0xfff;
  rsp->begin_instruction();
  rsp->decode(instruction);
  rsp->end_instruction();
}

void RspCompiler::Emitter::dma(Rsp *rsp, std::uint32_t clocks) {
  rsp->advance_dma(clocks);
}

void RspCompiler::Emitter::instruction(std::uint32_t opcode, std::uint32_t pc, bool branch) {
  const bool first = first_instruction;
  first_instruction = false;
  if (!branch && (integer(opcode) || memory(opcode, pc))) {
    if (first)
      op1(SLJIT_MOV32, gpr(0), imm(0));
    return;
  }
  if ((opcode >> 26) == 18 && ((opcode >> 21) & 31) >= 16) {
    if ((opcode & 63) == 55 || (opcode & 63) == 63) {
      if (first)
        op1(SLJIT_MOV32, gpr(0), imm(0));
      return;
    }
    op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S1));
    op1(SLJIT_MOV32, reg(SLJIT_R1), imm(opcode));
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(P, 32), SLJIT_IMM,
                     SLJIT_FUNC_ADDR(vector_helper));
    return;
  }
  op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S1));
  op1(SLJIT_MOV32, reg(SLJIT_R1), imm(opcode));
  op1(SLJIT_MOV32, reg(SLJIT_R2), imm(pc));
  sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(P, 32, 32), SLJIT_IMM,
                   branch ? SLJIT_FUNC_ADDR(branch_helper) : SLJIT_FUNC_ADDR(helper));
}

void RspCompiler::Emitter::commit(std::uint32_t pc, bool branch) {
  for (unsigned n = 0; n < 3; ++n) {
    const auto &stage = pipeline.previous[n];
    auto &dest = rsp.pipeline_.previous[n];
    op1(SLJIT_MOV32, field(&dest.gpr), imm(stage.gpr));
    op1(SLJIT_MOV32, field(&dest.vector), imm(stage.vector));
    op1(SLJIT_MOV_U8, field(&dest.load), imm(stage.load));
  }
  op1(SLJIT_MOV32, field(&rsp.pipeline_.clocks), imm(pipeline.clocks));
  op1(SLJIT_MOV_U8, field(&rsp.pipeline_.single_issue), imm(pipeline.single_issue));
  if (branch)
    return;
  op1(SLJIT_MOV32, field(&rsp.pc_), imm(pc));
  op1(SLJIT_MOV32, field(&rsp.pipeline_pc_), imm(pc));
  op1(SLJIT_MOV32, field(&rsp.next_pc_), imm((pc + 4) & 0xfff));
  op1(SLJIT_MOV_U8, field(&rsp.delay_slot_), imm(0));
  op1(SLJIT_MOV_U8, field(&rsp.next_delay_slot_), imm(0));
}

void RspCompiler::Emitter::budget_exit(std::uint32_t pc) {
  const auto jump = sljit_emit_cmp(compiler, SLJIT_SIG_GREATER_EQUAL, SLJIT_MEM0(),
                                   reinterpret_cast<sljit_sw>(&rsp.clock_), SLJIT_IMM, 0);
  exits.push_back({jump, pipeline, pc});
}

bool RspCompiler::Emitter::compile() {
  if (!compiler)
    return false;
  sljit_emit_enter(compiler, 0, SLJIT_ARGS0V(), 4, 2, 0);
  op1(SLJIT_MOV, reg(SLJIT_S0), imm(reinterpret_cast<std::uintptr_t>(&rsp.state_)));
  op1(SLJIT_MOV, reg(SLJIT_S1), imm(reinterpret_cast<std::uintptr_t>(&rsp)));
  for (unsigned n = 0; n < 128;) {
    const auto first_opcode = word(n);
    const auto first = Rsp::decode_info(first_opcode);
    auto second_opcode = 0u;
    Rsp::OpInfo second;
    bool dual = false;
    if (!pipeline.single_issue && !(first.flags & Rsp::Branch)) {
      second_opcode = word(n + 1);
      second = Rsp::decode_info(second_opcode);
      dual = Rsp::dual_issue(first, second);
    }
    pipeline.clocks = 0;
    pipeline.issue(first);
    if (dual)
      pipeline.issue(second);
    pipeline.end();
    const bool first_branch = first.flags & Rsp::Branch;
    const bool second_branch = dual && (second.flags & Rsp::Branch);
    instruction(first_opcode, (start + n * 4) & 0xfff, first_branch);
    if (dual)
      instruction(second_opcode, (start + (n + 1) * 4) & 0xfff, second_branch);
    n += dual ? 2 : 1;
    op2(SLJIT_ADD, field(&rsp.clock_), field(&rsp.clock_), imm(pipeline.clocks));
    const auto writes_io = [](std::uint32_t opcode) {
      return (opcode >> 26) == 16 && ((opcode >> 21) & 31) == 4;
    };
    const bool io = writes_io(first_opcode) || (dual && writes_io(second_opcode));
    const bool branch = first_branch || second_branch;
    if (branch || io || n >= 128) {
      commit((start + n * 4) & 0xfff, branch);
      if (io) {
        op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S1));
        op1(SLJIT_MOV32, reg(SLJIT_R1), imm(pipeline.clocks));
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(P, 32), SLJIT_IMM,
                         SLJIT_FUNC_ADDR(dma));
      }
      sljit_emit_return_void(compiler);
      break;
    }
    budget_exit((start + n * 4) & 0xfff);
  }
  for (const auto &exit : exits) {
    sljit_set_label(exit.jump, sljit_emit_label(compiler));
    pipeline = exit.pipeline;
    commit(exit.pc, false);
    sljit_emit_return_void(compiler);
  }
  for (const auto &path : memory_paths) {
    sljit_set_label(path.enter, sljit_emit_label(compiler));
    op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S1));
    op1(SLJIT_MOV32, reg(SLJIT_R1), imm(path.instruction));
    op1(SLJIT_MOV32, reg(SLJIT_R2), imm(path.pc));
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(P, 32, 32), SLJIT_IMM,
                     SLJIT_FUNC_ADDR(helper));
    sljit_set_label(sljit_emit_jump(compiler, SLJIT_JUMP), path.resume);
  }
  block.code = sljit_generate_code(compiler, 0, nullptr);
  block.bytes = static_cast<std::size_t>(sljit_get_generated_code_size(compiler));
  return block.code != nullptr;
}

} // namespace cupid::n64
