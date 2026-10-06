#pragma once

#include "core/cpu/cpu.hpp"
#include "core/cpu/execution/instruction.hpp"
#include "core/cpu/recompiler.hpp"
#include "core/memory/instruction_tracker.hpp"
#include <array>
#include <memory>
#include <optional>
#include <sljitLir.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cupid::n64 {

struct CpuCompiler::Impl {
  struct Key {
    std::uint64_t pc;
    unsigned mode;
    bool operator==(const Key &) const = default;
  };
  struct Hash {
    std::size_t operator()(const Key &key) const {
      return std::hash<std::uint64_t>()(key.pc) ^ (std::size_t(key.mode) << 1);
    }
  };
  struct Block {
    struct InstructionView {
      std::array<std::uint32_t, 8> words{};
      unsigned count = 0;
    };
    using Function = void (*)(const std::uint64_t *);
    std::vector<std::uint32_t> words;
    std::vector<unsigned> entries;
    std::vector<InstructionView> views;
    void *code = nullptr;
    std::size_t bytes = 0;
    ~Block() {
      if (code)
        sljit_free_code(code, nullptr);
    }
    void execute(const std::uint64_t &target) const {
      std::bit_cast<Function>(code)(&target);
    }
  };
  struct Entry {
    std::shared_ptr<Block> block;
    unsigned index = 0;
    std::uint64_t cache_generation = 0;
    bool tracked = false;
  };
  struct Section {
    std::unordered_map<Key, Entry, Hash> blocks;
    InstructionTracker *tracker = nullptr;
    std::uint64_t generation = 0;
    std::size_t bytes = 0;
  };
  Cpu &cpu;
  std::unordered_map<std::uint32_t, Section> sections;
  std::size_t bytes = 0;
  explicit Impl(Cpu &cpu) : cpu(cpu) {}
};

struct CpuCompiler::Emitter {
  struct Operand {
    sljit_s32 type;
    sljit_sw value = 0;
  };
  struct MemoryPath {
    std::vector<sljit_jump *> enter;
    sljit_label *resume;
    std::uint32_t instruction;
    bool defer_exit;
  };
  Cpu &cpu;
  Impl::Block &block;
  sljit_compiler *compiler;
  const std::uint64_t start_pc;
  std::uint64_t pc;
  std::uint32_t physical;
  bool wide;
  unsigned cycles = 0;
  bool pipeline_dirty = false;
  std::vector<MemoryPath> memory_paths;
  std::vector<bool> internal_entries;
  std::vector<sljit_label *> instruction_labels;
  std::vector<std::pair<sljit_jump *, unsigned>> internal_jumps;

  Emitter(Cpu &cpu, Impl::Block &block, std::uint64_t pc, std::uint32_t physical, bool wide);
  ~Emitter();
  bool compile();
  bool integer(std::uint32_t instruction);
  bool branch(std::uint32_t instruction);
  bool memory(std::uint32_t instruction, bool full, bool defer_exit);
  void memory_slow_paths();
  bool special(std::uint32_t instruction);
  void op1(sljit_s32 op, Operand dest, Operand source);
  void op2(sljit_s32 op, Operand dest, Operand left, Operand right);
  void compare(Operand dest, Operand left, Operand right, bool is_signed);
  void store(unsigned dest, Operand source, bool word = false);
  void commit_pipeline();
  void advance(unsigned clocks);
  void return_now(unsigned clocks);
  void return_if(sljit_s32 condition, Operand left, Operand right, unsigned clocks);
  void cache_guard(std::uint32_t address);
  std::optional<unsigned> internal_target(unsigned branch) const;
  std::optional<std::uint64_t> branch_target(unsigned branch) const;
  std::optional<unsigned> entry_index(std::uint64_t target) const;
  void plan_entries();
  void dispatch_internal(unsigned target);
  void execute(std::uint32_t instruction, bool defer_exit);
  void begin();
  void end(bool defer_exit);

  static Operand reg(sljit_s32 index) {
    return {index};
  }
  static Operand imm(std::uint64_t value) {
    return {SLJIT_IMM, static_cast<sljit_sw>(value)};
  }
  Operand field(const void *address) const {
    return {SLJIT_MEM1(SLJIT_S2),
            reinterpret_cast<sljit_sw>(address) - reinterpret_cast<sljit_sw>(&cpu)};
  }
  static Operand state(std::size_t offset) {
    return {SLJIT_MEM1(SLJIT_S1), static_cast<sljit_sw>(offset)};
  }
  static Operand gpr(unsigned index) {
    return state(offsetof(CpuState, gpr) + index * 8);
  }
  static sljit_sw guard(Cpu *cpu, std::uint32_t physical, const Impl::Block::InstructionView *view);
  static sljit_sw helper(Cpu *cpu, std::uint32_t instruction, sljit_uw clocks);
  static sljit_sw loop_pending(Cpu *cpu);
  static void step(Cpu *cpu, sljit_uw clocks);
};

} // namespace cupid::n64
