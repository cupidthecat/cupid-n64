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

void RspCompiler::Emitter::instruction(std::uint32_t opcode, std::uint32_t pc, bool branched,
                                       bool delay) {
  const auto operation = opcode >> 26;
  if (operation == 16 || operation == 50 || operation == 58 ||
      (operation == 0 && (opcode & 63) == 13))
    flush_clocks();
  if (branched && !delay) {
    op1(SLJIT_MOV32, field(&rsp.next_pc_), imm((pc + 8) & 0xfff));
    op1(SLJIT_MOV_U8, field(&rsp.next_delay_slot_), imm(0));
  }
  if (branched && branch(opcode, pc))
    return;
  if (vector_memory(opcode, pc))
    return;
  if (!branched && (integer(opcode) || memory(opcode, pc)))
    return;
  if (vector_arithmetic(opcode))
    return;
  op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S1));
  op1(SLJIT_MOV32, reg(SLJIT_R1), imm(opcode));
  op1(SLJIT_MOV32, reg(SLJIT_R2), imm(pc));
  sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3V(P, 32, 32), SLJIT_IMM,
                   SLJIT_FUNC_ADDR(helper));
}

bool RspCompiler::Emitter::compile() {
  if (!compiler)
    return false;
  const auto vectors = sljit_has_cpu_feature(SLJIT_HAS_SIMD) ? SLJIT_ENTER_VECTOR(2) : 0;
  sljit_emit_enter(compiler, 0, SLJIT_ARGS0V(), 4 | vectors, 3, 0);
  op1(SLJIT_MOV, reg(SLJIT_S0), imm(reinterpret_cast<std::uintptr_t>(&rsp.state_)));
  op1(SLJIT_MOV, reg(SLJIT_S1), imm(reinterpret_cast<std::uintptr_t>(&rsp)));
  op1(SLJIT_MOV, reg(SLJIT_S2), imm(reinterpret_cast<std::uintptr_t>(rsp.memory_.data())));
  op1(SLJIT_MOV32, gpr(0), imm(0));
  bool delay = false;
  for (unsigned n = 0; n < 1024;) {
    const auto first_opcode = word(n);
    const auto first = Rsp::decode_info(first_opcode);
    auto second_opcode = 0u;
    Rsp::OpInfo second;
    bool dual = false;
    if (!pipeline.single_issue && !(first.flags & Rsp::Branch) && n + 1 < 1024) {
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
    if (delay)
      begin_delay();
    instruction(first_opcode, (start + n * 4) & 0xfff, first_branch, delay);
    if (dual)
      instruction(second_opcode, (start + (n + 1) * 4) & 0xfff, second_branch, delay);
    n += dual ? 2 : 1;
    cycles += pipeline.clocks;
    if (delay) {
      flush_clocks();
      op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S1));
      sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1V(P), SLJIT_IMM,
                       SLJIT_FUNC_ADDR(end_delay));
      sljit_emit_return_void(compiler);
      break;
    }
    const bool branched = first_branch || second_branch;
    const auto next = (start + n * 4) & 0xfff;
    if (branched || n == 1024)
      commit(next, branched);
    const auto may_halt = [](std::uint32_t opcode) {
      return ((opcode >> 26) == 0 && (opcode & 63) == 13) ||
             ((opcode >> 26) == 16 && ((opcode >> 21) & 31) == 4 && ((opcode >> 11) & 15) == 4);
    };
    if (may_halt(first_opcode) || (dual && may_halt(second_opcode)))
      halt_exit(next, branched);
    if (n == 1024) {
      flush_clocks();
      sljit_emit_return_void(compiler);
      break;
    }
    delay = branched;
  }
  block.pipeline = pipeline;
  block.pipeline.clocks = 0;
  for (const auto &exit : exits) {
    sljit_set_label(exit.jump, sljit_emit_label(compiler));
    pipeline = exit.pipeline;
    cycles = exit.clocks;
    flush_clocks();
    commit_pipeline();
    commit(exit.pc, exit.branch);
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
