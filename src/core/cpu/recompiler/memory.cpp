#include "core/cpu/execution/memory.hpp"
#include "core/cpu/recompiler/compiler.hpp"

namespace cupid::n64 {

bool CpuCompiler::Emitter::memory(std::uint32_t instruction, bool full, bool defer_exit,
                                  bool delay) {
  const auto opcode = instruction >> 26;
  const auto memory = memory_instruction(instruction);
  if (!memory.bytes || (memory.wide && !wide))
    return false;
  const auto bytes = memory.bytes;
  const bool floating = memory.floating;
  const bool partial = memory.partial;
  const bool linked = memory.linked;
  if (floating && !(cpu.control_[Status] & 0x20000000))
    return false;
  const auto source = (instruction >> 21) & 31;
  const auto target = (instruction >> 16) & 31;
  const auto offset = static_cast<std::uint64_t>(
      std::int64_t(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(instruction))));
  const bool write = memory.write;
  auto transfer = gpr(target);
  if (floating) {
    transfer = state(offsetof(CpuState, fpr) + cpu.fpu_source(target) * 8);
    if (bytes == 4) {
      const bool high = !(cpu.control_[Status] & 0x04000000) && (target & 1);
      transfer.value += (high == (std::endian::native == std::endian::little)) ? 4 : 0;
    }
  }
  const auto ram_bytes = cpu.bus_.instruction_data(0).size_bytes();
  if (!ram_bytes)
    return false;
  const bool little = cpu.reverse_endian();
  const auto address = [&] {
    op2(SLJIT_ADD, reg(SLJIT_R0), gpr(source), imm(offset));
    op2(SLJIT_SUB, reg(SLJIT_R1), reg(SLJIT_R0), imm(0xffffffff80000000ull));
  };
  const auto line = [&] {
    if (little)
      op2(SLJIT_XOR, reg(SLJIT_R1), reg(SLJIT_R1), imm(8 - bytes));
    op2(SLJIT_LSHR, reg(SLJIT_R2), reg(SLJIT_R1), imm(4));
    op2(SLJIT_AND, reg(SLJIT_R2), reg(SLJIT_R2), imm(511));
    op2(SLJIT_MUL, reg(SLJIT_R2), reg(SLJIT_R2), imm(sizeof(Cpu::CacheLine)));
    op2(SLJIT_ADD, reg(SLJIT_R2), reg(SLJIT_R2),
        imm(reinterpret_cast<std::uintptr_t>(cpu.dcache_.data())));
  };
  commit_pipeline();
  SlowPath path{{}, nullptr, instruction, cycles, delay ? 0u : 2u};
  address();
  path.enter.push_back(
      sljit_emit_cmp(compiler, SLJIT_GREATER, SLJIT_R1, 0, SLJIT_IMM, ram_bytes - 1));
  if (bytes > 1 && !partial) {
    op2(SLJIT_AND, reg(SLJIT_R2), reg(SLJIT_R1), imm(bytes - 1));
    path.enter.push_back(sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_R2, 0, SLJIT_IMM, 0));
  }
  line();
  op1(SLJIT_MOV_U8, reg(SLJIT_R3), {SLJIT_MEM1(SLJIT_R2), offsetof(Cpu::CacheLine, valid)});
  path.enter.push_back(sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R3, 0, SLJIT_IMM, 0));
  op2(SLJIT_AND, reg(SLJIT_R3), reg(SLJIT_R1), imm(~0xfffull));
  path.enter.push_back(sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL | SLJIT_32, SLJIT_R3, 0,
                                      SLJIT_MEM1(SLJIT_R2), offsetof(Cpu::CacheLine, tag)));
  if (full)
    begin(false);
  // Cache hits keep the native instruction batch pending through the memory operation.
  advance(2);
  if (partial) {
    op1(SLJIT_MOV, reg(SLJIT_R3), reg(SLJIT_R1));
    op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S2));
    op1(SLJIT_MOV32, reg(SLJIT_R1), imm(instruction));
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS4V(P, 32, P, W), SLJIT_IMM,
                     SLJIT_FUNC_ADDR(cached_merge));
  } else {
    sljit_jump *unlinked = nullptr;
    if (linked) {
      if (write) {
        op1(SLJIT_MOV_U8, reg(SLJIT_R3), field(&cpu.llbit_));
        unlinked = sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R3, 0, SLJIT_IMM, 0);
      } else {
        op2(SLJIT_LSHR, reg(SLJIT_R3), reg(SLJIT_R1), imm(4));
        op1(SLJIT_MOV, field(&cpu.control_[LlAddr]), reg(SLJIT_R3));
        op1(SLJIT_MOV_U8, field(&cpu.llbit_), imm(1));
      }
    }
    if (write)
      op1(SLJIT_MOV_U8, {SLJIT_MEM1(SLJIT_R2), offsetof(Cpu::CacheLine, dirty)}, imm(1));
    op2(SLJIT_AND, reg(SLJIT_R3), reg(SLJIT_R1), imm(15));
    if constexpr (std::endian::native == std::endian::little) {
      if (bytes < 4)
        op2(SLJIT_XOR, reg(SLJIT_R3), reg(SLJIT_R3), imm(4 - bytes));
    }
    op2(SLJIT_ADD, reg(SLJIT_R2), reg(SLJIT_R2), reg(SLJIT_R3));
    const Operand data{SLJIT_MEM1(SLJIT_R2), offsetof(Cpu::CacheLine, words)};
    if (write) {
      op1(floating && bytes == 4 ? SLJIT_MOV_U32 : SLJIT_MOV, reg(SLJIT_R0), transfer);
      if (bytes == 8) {
        op2(SLJIT_LSHR, reg(SLJIT_R1), reg(SLJIT_R0), imm(32));
        op1(SLJIT_MOV32, data, reg(SLJIT_R1));
        op1(SLJIT_MOV32, {data.type, data.value + 4}, reg(SLJIT_R0));
      } else {
        op1(bytes == 1   ? SLJIT_MOV_U8
            : bytes == 2 ? SLJIT_MOV_U16
                         : SLJIT_MOV32,
            data, reg(SLJIT_R0));
      }
    } else {
      if (bytes == 8) {
        op1(SLJIT_MOV_U32, reg(SLJIT_R0), data);
        op1(SLJIT_MOV_U32, reg(SLJIT_R1), {data.type, data.value + 4});
        op2(SLJIT_SHL, reg(SLJIT_R0), reg(SLJIT_R0), imm(32));
        op2(SLJIT_OR, reg(SLJIT_R0), reg(SLJIT_R0), reg(SLJIT_R1));
      } else {
        const auto operation = bytes == 1 ? (opcode == 32 ? SLJIT_MOV_S8 : SLJIT_MOV_U8)
                               : bytes == 2
                                   ? (opcode == 33 ? SLJIT_MOV_S16 : SLJIT_MOV_U16)
                                   : (opcode == 35 || opcode == 48 ? SLJIT_MOV_S32 : SLJIT_MOV_U32);
        op1(operation, reg(SLJIT_R0), data);
      }
      if (floating)
        op1(bytes == 4 ? SLJIT_MOV32 : SLJIT_MOV, transfer, reg(SLJIT_R0));
      else
        store(target, reg(SLJIT_R0));
    }
    if (unlinked) {
      sljit_set_label(unlinked, sljit_emit_label(compiler));
      op1(SLJIT_MOV_U8, reg(SLJIT_R0), field(&cpu.llbit_));
      store(target, reg(SLJIT_R0));
    }
  }
  if (full)
    end(defer_exit, delay);
  else {
    cycles += 2;
    pipeline_dirty = true;
  }
  finish_slow_path(std::move(path), delay);
  return true;
}

void CpuCompiler::Emitter::finish_slow_path(SlowPath path, bool delay) {
  if (delay) {
    // Successful helpers skip clocks charged only by the native delay slot.
    advance(cycles);
    cycles = 0;
  }
  path.resume = sljit_emit_label(compiler);
  slow_paths.push_back(std::move(path));
}

void CpuCompiler::Emitter::emit_slow_paths() {
  for (const auto &path : slow_paths) {
    const auto entry = sljit_emit_label(compiler);
    for (auto jump : path.enter)
      sljit_set_label(jump, entry);
    op1(SLJIT_MOV, reg(SLJIT_R0), reg(SLJIT_S2));
    op1(SLJIT_MOV32, reg(SLJIT_R1), imm(path.instruction));
    op1(SLJIT_MOV, reg(SLJIT_R2), imm(path.clocks));
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS3(W, P, 32, W), SLJIT_IMM,
                     SLJIT_FUNC_ADDR(helper));
    op1(SLJIT_MOV, reg(SLJIT_S3), reg(SLJIT_R0));
    advance(path.completion_clocks);
    return_if(SLJIT_NOT_EQUAL, reg(SLJIT_S3), imm(0), 0);
    sljit_set_label(sljit_emit_jump(compiler, SLJIT_JUMP), path.resume);
  }
}

} // namespace cupid::n64
