#include "core/cpu/cpu.hpp"

namespace cupid::n64 {

void Cpu::advance_clocks(std::uint64_t clocks) {
  state_.clocks += clocks;
}

void Cpu::commit_count(std::uint64_t ticks) {
  constexpr std::uint64_t mask = (1ull << 33) - 1;
  const auto remaining = ((control_[Compare] << 1) - count_ticks_) & mask;
  if (remaining && ticks >= remaining)
    set_interrupt(7, true);
  count_ticks_ = (count_ticks_ + ticks) & mask;
}

void Cpu::flush_count() {
  const auto ticks = (state_.clocks - count_clock_) >> 1;
  count_clock_ += ticks << 1;
  commit_count(ticks);
}

void Cpu::synchronize_timer(const std::function<void()> &devices) {
  synchronize_timer([&] {
    if (devices)
      devices();
  });
}

} // namespace cupid::n64
