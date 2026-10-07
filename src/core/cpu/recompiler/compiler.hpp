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
  struct Block {
    struct InstructionView {
      std::array<std::uint32_t, 8> words{};
      unsigned count = 0;
    };
    using Function = void (*)(const std::uint64_t *);
    std::vector<std::uint32_t> words;
    std::vector<unsigned> entries;
    std::vector<InstructionView> views;
    std::array<std::uint32_t, 2> floating_control{};
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
    Key key;
    std::shared_ptr<Block> block;
    unsigned index = 0;
    std::uint64_t cache_generation = 0;
    bool tracked = false;
    std::unique_ptr<Entry> next;
  };
  struct Section {
    std::array<std::unique_ptr<Entry>, 1024> entries;
    InstructionTracker *tracker = nullptr;
    std::uint64_t generation = 0;
    std::size_t bytes = 0;
    Entry *find(const Key &key) const {
      auto *entry = entries[(key.pc >> 2) & 1023].get();
      while (entry && entry->key != key)
        entry = entry->next.get();
      return entry;
    }
    void insert(const Key &key, const std::shared_ptr<Block> &block, unsigned index = 0) {
      if (find(key))
        return;
      auto &head = entries[(key.pc >> 2) & 1023];
      auto entry = std::make_unique<Entry>();
      entry->key = key;
      entry->block = block;
      entry->index = index;
      entry->next = std::move(head);
      head = std::move(entry);
    }
    void erase(const std::shared_ptr<Block> &block) {
      for (auto &head : entries) {
        auto *entry = &head;
        while (*entry) {
          if ((*entry)->block == block) {
            auto next = std::move((*entry)->next);
            *entry = std::move(next);
          } else
            entry = &(*entry)->next;
        }
      }
    }
  };
  Cpu &cpu;
  std::array<std::unique_ptr<Section>, 2048> sections;
  std::unordered_map<std::uint32_t, Section> other_sections;
  std::size_t bytes = 0;
  explicit Impl(Cpu &cpu) : cpu(cpu) {}
  Section &section(std::uint32_t page) {
    const auto index = page >> 12;
    if (index >= sections.size())
      return other_sections[page];
    auto &section = sections[index];
    if (!section)
      section = std::make_unique<Section>();
    return *section;
  }
};

struct CpuCompiler::Emitter {
  struct Operand {
    sljit_s32 type;
    sljit_sw value = 0;
  };
  struct SlowPath {
    std::vector<sljit_jump *> enter;
    sljit_label *resume;
    std::uint32_t instruction;
    unsigned clocks;
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
  std::vector<SlowPath> slow_paths;
  std::vector<bool> internal_entries;
  std::vector<sljit_label *> instruction_labels;
  std::vector<std::pair<sljit_jump *, unsigned>> internal_jumps;

  Emitter(Cpu &cpu, Impl::Block &block, std::uint64_t pc, std::uint32_t physical, bool wide);
  ~Emitter();
  bool compile();
  bool integer(std::uint32_t instruction);
  bool branch(std::uint32_t instruction);
  bool memory(std::uint32_t instruction, bool full, bool defer_exit);
  void emit_slow_paths();
  bool floating(std::uint32_t instruction, bool full, bool defer_exit);
  void floating_transfer(std::uint32_t instruction);
  void floating_arithmetic(SlowPath &path);
  void floating_convert(SlowPath &path);
  void floating_compare(SlowPath &path);
  void floating_input(SlowPath &path, sljit_s32 source, bool dual, bool subnormal = true);
  void floating_flags(SlowPath &path, bool dual, bool flush);
  void floating_environment(unsigned rounding);
  void floating_restore();
  void floating_sqrt(bool dual);
  void floating_to_integer(bool dual, bool integer_dual);
  static bool floating_host();
  static Operand fpr(unsigned index, bool word = false, bool high = false);
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
  void begin(bool flush = true);
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
