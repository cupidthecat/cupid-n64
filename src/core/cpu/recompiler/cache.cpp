#include "core/cpu/recompiler/compiler.hpp"
#include <algorithm>

namespace cupid::n64 {

CpuCompiler::CpuCompiler(Cpu &cpu) : impl_(std::make_unique<Impl>(cpu)) {}
CpuCompiler::~CpuCompiler() = default;

void CpuCompiler::reset() {
  impl_->blocks.clear();
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
  const auto data = cpu.bus_.instruction_data(page);
  if (data.size() < 1024)
    return false;
  const auto first = (physical & 4095) >> 2;
  const unsigned reverse = cpu.little_endian() ? 1 : 0;
  const bool wide = cpu.mode() == Cpu::Mode::Kernel || cpu.extended_addressing();
  const Impl::Key key{cpu.state_.pc, reverse | (unsigned(wide) << 1)};
  auto found = impl_->blocks.find(key);
  if (found != impl_->blocks.end()) {
    const auto &words = found->second->words;
    bool unchanged = true;
    for (unsigned n = 0; n < words.size(); ++n)
      if (words[n] != data[(first + n) ^ reverse]) {
        unchanged = false;
        break;
      }
    if (!unchanged) {
      impl_->bytes -= found->second->bytes;
      impl_->blocks.erase(found);
      found = impl_->blocks.end();
    }
  }
  const auto limit = found == impl_->blocks.end()
                         ? 1024u
                         : first + static_cast<unsigned>(found->second->words.size());
  std::vector<std::uint32_t> words;
  bool stop_after_delay = false;
  for (unsigned word = first; word < limit; ++word) {
    const auto address = page + word * 4;
    if (word == first || !(word & 7)) {
      const auto virtual_address = cpu.state_.pc + (word - first) * 4;
      const auto &line = cpu.icache_[(virtual_address >> 5) & 511];
      if (line.hit(address) &&
          !std::equal(line.words.begin(), line.words.end(), data.begin() + (word & ~7u)))
        return false;
    }
    if (found != impl_->blocks.end())
      continue;
    const auto instruction = data[word ^ reverse];
    words.push_back(instruction);
    const auto info = block_instruction(instruction);
    if (stop_after_delay || (!info.branch && info.terminal))
      break;
    stop_after_delay = info.stop_after_delay;
  }
  if (found == impl_->blocks.end()) {
    if (impl_->bytes >= 32 * 1024 * 1024)
      reset();
    auto block = std::make_unique<Impl::Block>();
    block->words = std::move(words);
    Emitter emitter(cpu, *block, key.pc, physical, wide);
    if (!emitter.compile())
      return cpu.run_interpreted_block(clock_target);
    impl_->bytes += block->bytes;
    found = impl_->blocks.emplace(key, std::move(block)).first;
  }
  found->second->execute(clock_target);
  return true;
#endif
}

} // namespace cupid::n64
