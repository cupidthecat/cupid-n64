#include "core/debug/trace.hpp"
#include <stdexcept>

namespace cupid::n64 {

void TraceLog::configure(TraceConfig config) {
  constexpr auto known = (trace_bit(TraceKind::Tlb) << 1) - 1;
  if ((config.mask & ~known) || !config.capacity || config.capacity > maximum_records ||
      config.cpu.first > config.cpu.last || config.rsp.first > config.rsp.last ||
      config.rsp.last > 0xfff)
    throw std::runtime_error("Invalid instruction trace settings.");
  std::vector<TraceRecord> replacement(config.mask ? config.capacity : 0);
  storage_.swap(replacement);
  config_ = config;
  clear();
}

void TraceLog::record(TraceRecord value) noexcept {
  if (!enabled(value.kind))
    return;
  const auto range = value.kind == TraceKind::RspInstruction ? config_.rsp : config_.cpu;
  if (value.pc < range.first || value.pc > range.last)
    return;
  value.sequence = ++sequence_;
  if (count_ < storage_.size()) {
    storage_[(first_ + count_) % storage_.size()] = value;
    ++count_;
  } else {
    storage_[first_] = value;
    first_ = (first_ + 1) % static_cast<unsigned>(storage_.size());
    ++dropped_;
  }
}

void TraceLog::clear() noexcept {
  first_ = count_ = 0;
  sequence_ = dropped_ = 0;
}

std::vector<TraceRecord> TraceLog::records() const {
  std::vector<TraceRecord> result;
  result.reserve(count_);
  for (unsigned n = 0; n < count_; ++n)
    result.push_back(storage_[(first_ + n) % storage_.size()]);
  return result;
}

} // namespace cupid::n64
