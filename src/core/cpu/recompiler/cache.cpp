#include "core/cpu/recompiler/compiler.hpp"
#include <algorithm>

namespace cupid::n64 {

CpuCompiler::CpuCompiler(Cpu &cpu) : impl_(std::make_unique<Impl>(cpu)) {}
CpuCompiler::~CpuCompiler() = default;

void CpuCompiler::reset() {
  for (auto &section : impl_->sections)
    section.reset();
  impl_->other_sections.clear();
  impl_->bytes = 0;
}

bool CpuCompiler::run(const std::uint64_t &clock_target) {
#if !SLJIT_64BIT_ARCHITECTURE || SLJIT_CONFIG_UNSUPPORTED
  return impl_->cpu.run_interpreted_block(clock_target);
#else
  auto &cpu = impl_->cpu;
  if (cpu.poll_interrupt()) {
    if (cpu.synchronize_)
      cpu.synchronize_();
    return true;
  }
  if (cpu.bus_.frozen() || (cpu.state_.pc & 3))
    return false;
  if (!cpu.extended_addressing() &&
      sign_word(static_cast<std::uint32_t>(cpu.state_.pc)) != cpu.state_.pc)
    return false;
  std::uint32_t physical = 0;
  switch (cpu.segment(cpu.state_.pc)) {
  case Cpu::Segment::Cached:
    physical = static_cast<std::uint32_t>(cpu.state_.pc & 0x1fffffff);
    break;
  case Cpu::Segment::Cached32:
    physical = static_cast<std::uint32_t>(cpu.state_.pc);
    break;
  default:
    return false;
  }
  const auto page = physical & ~4095u;
  const auto first = (physical & 4095) >> 2;
  const unsigned reverse = cpu.little_endian() ? 1 : 0;
  const bool wide = cpu.mode() == Cpu::Mode::Kernel || cpu.extended_addressing();
  const auto floating_mode = static_cast<unsigned>(cpu.control_[Status] & 0x24000000);
  const auto floating_control =
      ((cpu.state_.fcr31 & 0x00000f83) << 2) | (cpu.state_.fcr31 & 0x01000000);
  const Impl::Key key{cpu.state_.pc,
                      reverse | (unsigned(wide) << 1) | floating_mode | floating_control};
  if (impl_->bytes >= 63 * 1024 * 1024)
    reset();
  auto &section = impl_->section(page);
  auto *tracker = cpu.bus_.instruction_tracker();
  const auto generation = tracker ? tracker->generation(page) : 0;
  if (tracker && section.tracker &&
      (section.tracker != tracker || section.generation != generation)) {
    impl_->bytes -= section.bytes;
    section = {};
  }
  if (tracker) {
    section.tracker = tracker;
    section.generation = generation;
  }
  auto *found = section.find(key);
  // RAM writes cannot replace instructions already held in an unchanged CPU cache.
  const bool cache_unchanged =
      found && found->cache_generation == cpu.instruction_cache_generation_;
  std::span<const std::uint32_t> data;
  if (!cache_unchanged) {
    data = cpu.bus_.instruction_data(page);
    if (data.size() < 1024)
      return false;
  }
  if (found && !cache_unchanged && (!tracker || !found->tracked)) {
    const auto words = std::span(found->block->words).subspan(found->index);
    bool unchanged = !reverse && std::equal(words.begin(), words.end(), data.begin() + first);
    if (reverse) {
      unchanged = true;
      for (unsigned n = 0; n < words.size(); ++n)
        if (words[n] != data[(first + n) ^ reverse]) {
          unchanged = false;
          break;
        }
    }
    if (!unchanged) {
      const auto owner = found->block;
      impl_->bytes -= owner->bytes;
      section.bytes -= owner->bytes;
      section.erase(owner);
      found = nullptr;
    }
  }
  const auto limit =
      found ? first + static_cast<unsigned>(found->block->words.size()) - found->index : 1024u;
  std::vector<std::uint32_t> words;
  bool stop_after_delay = false;
  const bool validate = !cache_unchanged;
  for (unsigned word = first; validate && word < limit; ++word) {
    const auto address = page + word * 4;
    if (word == first || !(word & 7)) {
      const auto virtual_address = cpu.state_.pc + (word - first) * 4;
      const auto &line = cpu.icache_[(virtual_address >> 5) & 511];
      if (line.hit(address) &&
          !std::equal(line.words.begin(), line.words.end(), data.begin() + (word & ~7u)))
        return false;
    }
    if (found) {
      word |= 7;
      continue;
    }
    const auto instruction = data[word ^ reverse];
    words.push_back(instruction);
    const auto info = block_instruction(instruction);
    if (stop_after_delay || (!info.branch && info.terminal) || (instruction >> 26) == 47)
      break;
    stop_after_delay = info.stop_after_delay;
  }
  if (!found) {
    auto block = std::make_shared<Impl::Block>();
    block->words = std::move(words);
    Emitter emitter(cpu, *block, key.pc, physical, wide);
    if (!emitter.compile())
      return cpu.run_interpreted_block(clock_target);
    impl_->bytes += block->bytes;
    section.bytes += block->bytes;
    if (tracker)
      tracker->watch(physical, static_cast<std::uint32_t>(block->words.size() * 4));
    section.insert(key, block);
    for (auto index : block->entries)
      section.insert(Impl::Key{key.pc + index * 4, key.mode}, block, index);
    found = section.find(key);
  }
  if (tracker && !found->tracked)
    tracker->watch(physical,
                   static_cast<std::uint32_t>((found->block->words.size() - found->index) * 4));
  found->tracked = tracker != nullptr;
  found->cache_generation = cpu.instruction_cache_generation_;
  found->block->execute(clock_target);
  return true;
#endif
}

} // namespace cupid::n64
