#include "core/cpu/cpu.hpp"
#include "core/cpu/execution/instruction.hpp"
#include "core/cpu/recompiler.hpp"
#include <algorithm>

namespace cupid::n64 {

std::uint64_t Cpu::synchronization_limit() const {
  const auto count = (count_ticks_ + ((state_.clocks - count_clock_) >> 1)) & ((1ull << 33) - 1);
  const auto remaining =
      static_cast<std::int64_t>(control_[Compare] << 1) - static_cast<std::int64_t>(count);
  return remaining > 0 ? static_cast<std::uint64_t>(remaining) : 0;
}

void Cpu::interrupt_changed() {
  if ((control_[Status] & 7) == 1 && (control_[Status] & control_[Cause] & 0xff00) && synchronize_)
    synchronize_();
}

bool Cpu::run_block(const std::uint64_t &clock_target) {
  return compiler_->run(clock_target);
}

bool Cpu::run_interpreted_block(const std::uint64_t &clock_target) {
  if (poll_interrupt()) {
    if (synchronize_)
      synchronize_();
    return true;
  }
  if (bus_.frozen() || (state_.pc & 3))
    return false;
  if (!extended_addressing() && sign_word(static_cast<std::uint32_t>(state_.pc)) != state_.pc)
    return false;
  std::uint32_t physical = 0;
  switch (segment(state_.pc)) {
  case Segment::Cached:
    physical = static_cast<std::uint32_t>(state_.pc & 0x1fffffff);
    break;
  case Segment::Cached32:
    physical = static_cast<std::uint32_t>(state_.pc);
    break;
  default:
    return false;
  }
  const auto page = physical & ~4095u;
  const auto data = bus_.instruction_data(page);
  if (data.size() < 1024)
    return false;
  const auto start_pc = state_.pc;
  const auto first = (physical & 4095) >> 2;
  const unsigned reverse = little_endian() ? 1 : 0;
  std::array<std::uint32_t, 1024> instructions;
  unsigned count = 0;
  bool stop_after_delay = false;
  for (unsigned word = first; word < 1024; ++word) {
    const auto address = page + word * 4;
    if (word == first || !(word & 7)) {
      const auto virtual_address = start_pc + (word - first) * 4;
      const auto &line = icache_[(virtual_address >> 5) & 511];
      if (line.hit(address) &&
          !std::equal(line.words.begin(), line.words.end(), data.begin() + (word & ~7u)))
        return false;
    }
    const auto instruction = data[word ^ reverse];
    instructions[count++] = instruction;
    const auto info = block_instruction(instruction);
    // Cache operations can change instructions already captured for this block.
    if (stop_after_delay || (!info.branch && info.terminal) || (instruction >> 26) == 47)
      break;
    stop_after_delay = info.stop_after_delay;
  }
  const auto end_pc = start_pc + count * 4;
  bool first_instruction = true;
  bool conditional_delay = false;
  while (state_.pc >= start_pc && state_.pc < end_pc) {
    const auto pc = state_.pc;
    const auto index = static_cast<unsigned>((pc - start_pc) >> 2);
    auto instruction = instructions[index];
    bool changed = false;
    if (first_instruction || !(pc & 31)) {
      const auto address = physical + static_cast<std::uint32_t>(pc - start_pc);
      auto &line = icache_[(pc >> 5) & 511];
      if (!line.hit(address)) {
        if (!fill(line, address, static_cast<std::uint32_t>(pc) & 0xfe0, true)) {
          advance_clocks(2);
          return true;
        }
        const auto remaining = std::min(count - index, 8 - ((address >> 2) & 7));
        for (unsigned n = 0; n < remaining; ++n)
          changed |= instructions[index + n] != line.words[(((address >> 2) + n) ^ reverse) & 7];
        if (changed)
          instruction = line.words[((address >> 2) ^ reverse) & 7];
      }
    }
    const auto info = block_instruction(instruction);
    begin_instruction();
    decode(instruction);
    advance_clocks(2);
    const auto self_jump = (2u << 26) | static_cast<std::uint32_t>((pc >> 2) & 0x03ffffff);
    if (instruction == 0x1000ffff || instruction == self_jump)
      advance_clocks(126);
    const bool exit = block_exit_;
    end_instruction();
    if (changed || exit || (!info.branch && info.terminal) || (instruction >> 26) == 47 ||
        (conditional_delay && state_.clocks >= clock_target))
      return true;
    conditional_delay = info.branch && !info.stop_after_delay && (instruction >> 26) != 3 &&
                        !((instruction >> 26) == 0 && (instruction & 63) == 9);
    first_instruction = false;
  }
  return true;
}

} // namespace cupid::n64
