#pragma once

#include "core/rsp/recompiler.hpp"
#include "core/rsp/recompiler/constants.hpp"
#include "core/rsp/rsp.hpp"
#include <bit>
#include <bitset>
#include <cstddef>
#include <optional>
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
    Key(const Rsp::Pipeline &pipeline, std::uint32_t address, bool tracing = false) : pc(address) {
      flags = static_cast<std::uint8_t>((unsigned(pipeline.single_issue) << 3) |
                                        (unsigned(tracing) << 4));
      for (unsigned n = 0; n < registers.size(); ++n) {
        const auto &stage = pipeline.previous[n];
        registers[n] = stage.gpr | (std::uint64_t(stage.vector) << 32);
        flags |= static_cast<std::uint8_t>(unsigned(stage.load) << n);
      }
    }
    bool tracing() const {
      return flags & 16;
    }
    bool operator==(const Key &) const = default;
  };
  struct Hash {
    std::size_t operator()(const Key &key) const {
      std::uint64_t value = (std::uint64_t(key.pc) << 5) | key.flags;
      for (const auto registers : key.registers)
        value = std::rotl(value, 17) ^ registers;
      return std::hash<std::uint64_t>()(value);
    }
  };
  struct ExecutionRecord {
    void *code = nullptr;
    std::uint32_t size = 0;
    Rsp::Pipeline pipeline;
  };
  struct Block {
    using Function = void (*)();
    Key key;
    ExecutionRecord record;
    std::vector<std::uint32_t> words;
    std::bitset<64> lines;
    std::size_t bytes = 0;
    std::uint64_t generation = 0;
    std::unique_ptr<Block> diagnosed;
    bool homebrew = false;
    ~Block() {
      if (record.code)
        sljit_free_code(record.code, nullptr);
    }
    void execute(Rsp &rsp) const {
      const auto before = rsp.clock_;
      rsp.pipeline_ = record.pipeline;
      std::bit_cast<Function>(record.code)();
      rsp.pipeline_.clocks_total +=
          static_cast<std::uint32_t>(rsp.clock_ - before) + rsp.pipeline_.clocks;
    }
  };
  static constexpr std::size_t capacity = 1024 * 1024;
  static constexpr std::size_t reserve = 128 * 1024;
  static constexpr std::size_t aligned(std::size_t bytes) {
    return (bytes + 15) & ~std::size_t(15);
  }
  static std::size_t footprint(const Block &block) {
    return aligned(sizeof(ExecutionRecord)) + aligned(block.bytes);
  }
  Rsp &rsp;
  std::unordered_map<Key, std::vector<std::unique_ptr<Block>>, Hash> blocks;
  std::array<std::array<Block *, 1024>, 2> context{};
  std::bitset<64> dirty;
  bool external_memory = false;
  std::size_t bytes = 0;
  std::size_t instrumented_bytes = 0;
  std::uint64_t generation = 1;
  explicit Impl(Rsp &rsp) : rsp(rsp) {}
  void reset();
  void clear_instrumented();
  void clear_dirty();
  bool matches(Block &block);
  Block *select();
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
  RspConstants constants;
  std::optional<std::uint32_t> vector_base;
  std::uint32_t vector_window_base = 0;
  std::uint32_t vector_window_offset = 0;
  bool tracing;
  bool diagnostics;
  std::vector<Exit> exits;
  std::vector<MemoryPath> memory_paths;

  Emitter(Rsp &rsp, Impl::Block &block, bool tracing = false, bool diagnostics = false);
  ~Emitter();
  bool compile();
  std::uint32_t word(unsigned index);
  bool integer(std::uint32_t instruction);
  bool memory(std::uint32_t instruction, std::uint32_t pc);
  bool vector_arithmetic(std::uint32_t instruction);
  bool vector_arithmetic_simd(std::uint32_t instruction);
  bool vector_memory(std::uint32_t instruction, std::uint32_t pc);
  void vector_linear(std::uint32_t instruction, std::uint32_t pc);
  void vector_quad(std::uint32_t instruction);
  bool vector_quad_simd(std::uint32_t instruction);
  void vector_packed(std::uint32_t instruction);
  void vector_transpose(std::uint32_t instruction);
  static Operand vector_byte(unsigned index, unsigned byte);
  static Operand vector_half(unsigned index, unsigned lane);
  void vector_address(unsigned offset, bool window = false);
  Operand vector_location(unsigned offset, bool window = false);
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
  static void profile_clocks(Rsp *rsp, std::uint32_t clocks);
  static void trace(Rsp *rsp, std::uint32_t instruction, std::uint32_t pc, std::uint32_t clocks);
  static void diagnostic_trace(Rsp *rsp, std::uint32_t instruction, std::uint32_t pc,
                               std::uint32_t clocks);

  static Operand reg(sljit_s32 index) {
    return {index};
  }
  static Operand imm(std::uint64_t value) {
    return {SLJIT_IMM, static_cast<sljit_sw>(value)};
  }
  Operand field(const void *address) const {
    const auto location = reinterpret_cast<std::uintptr_t>(address);
    const auto base = reinterpret_cast<std::uintptr_t>(&rsp);
    if (location >= base && location - base < sizeof(Rsp))
      return {SLJIT_MEM1(SLJIT_S1), static_cast<sljit_sw>(location - base)};
    return {SLJIT_MEM0(), static_cast<sljit_sw>(location)};
  }
  static Operand gpr(unsigned index) {
    return {SLJIT_MEM1(SLJIT_S0), static_cast<sljit_sw>(offsetof(RspState, gpr) + index * 4)};
  }
};

} // namespace cupid::n64
