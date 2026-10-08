#pragma once

#include "core/rsp/recompiler.hpp"
#include "core/rsp/rsp.hpp"
#include <bit>
#include <bitset>
#include <cstddef>
#include <sljitLir.h>
#include <unordered_map>
#include <vector>

namespace cupid::n64 {

struct RspCompiler::Impl {
  struct Key {
    std::array<std::uint64_t, 3> registers{};
    std::uint32_t pc = 0;
    std::uint8_t flags = 0;
    Key() = default;
    Key(const Rsp::Pipeline &pipeline, std::uint32_t address) : pc(address) {
      flags = static_cast<std::uint8_t>(unsigned(pipeline.single_issue) << 3);
      for (unsigned n = 0; n < registers.size(); ++n) {
        const auto &stage = pipeline.previous[n];
        registers[n] = stage.gpr | (std::uint64_t(stage.vector) << 32);
        flags |= static_cast<std::uint8_t>(unsigned(stage.load) << n);
      }
    }
    bool operator==(const Key &) const = default;
  };
  struct Hash {
    std::size_t operator()(const Key &key) const {
      std::uint64_t value = (std::uint64_t(key.pc) << 4) | key.flags;
      for (const auto registers : key.registers)
        value = std::rotl(value, 17) ^ registers;
      return std::hash<std::uint64_t>()(value);
    }
  };
  struct Block {
    using Function = void (*)();
    Key key;
    Rsp::Pipeline pipeline;
    std::vector<std::uint32_t> words;
    std::bitset<128> lines;
    void *code = nullptr;
    std::size_t bytes = 0;
    std::uint64_t generation = 0;
    ~Block() {
      if (code)
        sljit_free_code(code, nullptr);
    }
    void execute(Rsp &rsp) const {
      rsp.pipeline_ = pipeline;
      std::bit_cast<Function>(code)();
    }
  };
  Rsp &rsp;
  std::unordered_map<Key, std::vector<std::unique_ptr<Block>>, Hash> blocks;
  std::array<Block *, 1024> context{};
  std::array<Block *, 1024> alternate{};
  std::bitset<128> dirty;
  bool external_memory = false;
  std::size_t bytes = 0;
  std::uint64_t generation = 1;
  explicit Impl(Rsp &rsp) : rsp(rsp) {}
};

struct RspCompiler::Emitter {
  struct Operand {
    sljit_s32 type;
    sljit_sw value = 0;
  };
  struct Exit {
    sljit_jump *jump;
    std::uint32_t pc;
    unsigned clocks;
    bool branch;
  };
  struct MemoryPath {
    sljit_jump *enter;
    sljit_label *resume;
    std::uint32_t instruction;
    std::uint32_t pc;
  };
  Rsp &rsp;
  Impl::Block &block;
  sljit_compiler *compiler;
  Rsp::Pipeline pipeline;
  std::uint32_t start;
  unsigned cycles = 0;
  std::vector<Exit> exits;
  std::vector<MemoryPath> memory_paths;

  Emitter(Rsp &rsp, Impl::Block &block);
  ~Emitter();
  bool compile();
  std::uint32_t word(unsigned index);
  bool integer(std::uint32_t instruction);
  bool memory(std::uint32_t instruction, std::uint32_t pc);
  bool vector_arithmetic(std::uint32_t instruction);
  bool vector_memory(std::uint32_t instruction, std::uint32_t pc);
  void vector_linear(std::uint32_t instruction, std::uint32_t pc);
  void vector_quad(std::uint32_t instruction);
  bool vector_quad_simd(std::uint32_t instruction);
  void vector_packed(std::uint32_t instruction);
  void vector_transpose(std::uint32_t instruction);
  static Operand vector_byte(unsigned index, unsigned byte);
  static Operand vector_half(unsigned index, unsigned lane);
  void vector_address(unsigned offset, bool window = false);
  bool branch(std::uint32_t instruction, std::uint32_t pc);
  void instruction(std::uint32_t instruction, std::uint32_t pc, bool branch, bool delay);
  void commit(std::uint32_t pc, bool branch);
  void flush_clocks();
  void begin_delay();
  void halt_exit(std::uint32_t pc, bool branch);
  void op1(sljit_s32 op, Operand dest, Operand source);
  void op2(sljit_s32 op, Operand dest, Operand left, Operand right);
  void compare(unsigned dest, Operand left, Operand right, bool is_signed);
  void store(unsigned dest, Operand source);
  static void helper(Rsp *rsp, std::uint32_t instruction, std::uint32_t pc);
  static void end_delay(Rsp *rsp);

  static Operand reg(sljit_s32 index) {
    return {index};
  }
  static Operand imm(std::uint64_t value) {
    return {SLJIT_IMM, static_cast<sljit_sw>(value)};
  }
  static Operand field(const void *address) {
    return {SLJIT_MEM0(), reinterpret_cast<sljit_sw>(address)};
  }
  static Operand gpr(unsigned index) {
    return {SLJIT_MEM1(SLJIT_S0), static_cast<sljit_sw>(offsetof(RspState, gpr) + index * 4)};
  }
};

} // namespace cupid::n64
