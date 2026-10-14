#include "core/debug/diagnostics.hpp"
#include "core/system/console.hpp"
#include <stdexcept>

namespace cupid::n64 {

void Diagnostics::connect(Console &console, Cpu &cpu, TraceLog &trace) {
  console_ = &console;
  cpu_ = &cpu;
  trace_ = &trace;
}

void Diagnostics::configure(DiagnosticConfig config) {
  constexpr auto known = (diagnostic_bit(DiagnosticDevice::Disk) << 1) - 1;
  if ((config.devices & ~known) || !config.capacity || config.capacity > maximum_records ||
      config.first_address > config.last_address || config.cpu.first > config.cpu.last ||
      config.rsp.first > config.rsp.last || config.rsp.last > 0xfff)
    throw std::runtime_error("Invalid device diagnostic settings.");
  std::vector<DiagnosticRecord> replacement(
      (homebrew_ || config.devices || config.coherence) ? config.capacity : 0);
  auto local = (homebrew_ || config.coherence) ? std::make_unique<std::array<LocalContext, 4096>>()
                                               : nullptr;
  auto cache =
      (homebrew_ || config.coherence) ? std::make_unique<std::array<CacheContext, 512>>() : nullptr;
  synchronize_cpu();
  storage_.swap(replacement);
  local_.swap(local);
  cache_.swap(cache);
  config_ = config;
  clear_log();
  reset_context();
}

void Diagnostics::set_homebrew(bool enabled) {
  if (enabled == homebrew_)
    return;
  if (enabled && storage_.empty())
    storage_.resize(config_.capacity);
  if (enabled && !local_)
    local_ = std::make_unique<std::array<LocalContext, 4096>>();
  if (enabled && !cache_)
    cache_ = std::make_unique<std::array<CacheContext, 512>>();
  if (!enabled && emux_trace_) {
    trace_->set_enabled(TraceKind::RspInstruction, previous_rsp_trace_);
    emux_trace_ = false;
  }
  synchronize_cpu();
  homebrew_ = enabled;
  exception_mask_ = 0;
  trace_countdown_ = 0;
  control_ = HostControl::None;
  slots_.clear();
  reset_context();
}

void Diagnostics::stop() {
  synchronize_cpu();
  set_homebrew(false);
  config_.profiling = config_.coherence = false;
  config_.devices = 0;
}

void Diagnostics::power() {
  if (emux_trace_) {
    trace_->set_enabled(TraceKind::RspInstruction, previous_rsp_trace_);
    emux_trace_ = false;
  }
  clear_log();
  clear_profiles();
  reset_context();
  exception_mask_ = 0;
  trace_countdown_ = 0;
  control_ = HostControl::None;
}

void Diagnostics::clear_log() noexcept {
  for (auto &record : storage_)
    record.text.clear();
  first_ = count_ = 0;
  sequence_ = dropped_ = 0;
}

void Diagnostics::clear_profiles() noexcept {
  profile_ = {};
  committed_cpu_cycles_ = {};
  cpu_profile_clock_ = cpu_ ? cpu_->state().clocks : 0;
  slots_.clear();
}

void Diagnostics::reset_context() noexcept {
  if (cache_)
    cache_->fill({});
  if (local_)
    local_->fill({});
  dma_origins_ = {};
  origin_ = {};
  refresh_warned_ = false;
  control_ = HostControl::None;
  cpu_profile_clock_ = cpu_ ? cpu_->state().clocks : 0;
}

DiagnosticOrigin Diagnostics::origin() const noexcept {
  if (origin_.known)
    return origin_;
  return cpu_ ? DiagnosticOrigin{DebugProcessor::Cpu, cpu_->state().pc, cpu_->state().clocks, true}
              : DiagnosticOrigin{};
}

bool Diagnostics::accepts(DiagnosticOrigin origin) const noexcept {
  if (!origin.known)
    return true;
  const auto range = origin.processor == DebugProcessor::Rsp ? config_.rsp : config_.cpu;
  return origin.pc >= range.first && origin.pc <= range.last;
}

void Diagnostics::record(DiagnosticRecord value) noexcept {
  if (storage_.empty() || !accepts(value.origin) || value.address < config_.first_address ||
      value.address > config_.last_address)
    return;
  value.sequence = ++sequence_;
  if (count_ < storage_.size()) {
    storage_[(first_ + count_) % storage_.size()] = std::move(value);
    ++count_;
  } else {
    storage_[first_] = std::move(value);
    first_ = (first_ + 1) % static_cast<unsigned>(storage_.size());
    ++dropped_;
  }
}

std::vector<DiagnosticRecord> Diagnostics::records() const {
  std::vector<DiagnosticRecord> result;
  result.reserve(count_);
  for (unsigned n = 0; n < count_; ++n)
    result.push_back(storage_[(first_ + n) % storage_.size()]);
  return result;
}

void Diagnostics::register_access(DiagnosticDevice device, std::uint32_t address,
                                  std::uint64_t value, bool write, unsigned bytes) noexcept {
  if (config_.devices & diagnostic_bit(device))
    record({.kind = write ? DiagnosticKind::RegisterWrite : DiagnosticKind::RegisterRead,
            .device = device,
            .origin = origin(),
            .value = value,
            .address = address,
            .bytes = bytes});
}

void Diagnostics::set_dma_origin(MemoryRequestor requestor, DiagnosticOrigin value) noexcept {
  const auto index = static_cast<unsigned>(requestor);
  if (index < dma_origins_.size())
    dma_origins_[index] = value;
}

DiagnosticOrigin Diagnostics::dma_origin(MemoryRequestor requestor) const noexcept {
  const auto index = static_cast<unsigned>(requestor);
  return index < dma_origins_.size() ? dma_origins_[index] : DiagnosticOrigin{};
}

void Diagnostics::text(DebugProcessor processor, std::string message) {
  if (message.empty())
    return;
  auto context = origin();
  context.processor = processor;
  if (processor == DebugProcessor::Rsp && console_)
    context.pc = console_->signal().pc();
  record({.kind = DiagnosticKind::HomebrewLog, .origin = context, .text = std::move(message)});
}

HostControl Diagnostics::take_control() noexcept {
  const auto control = control_;
  control_ = HostControl::None;
  return control;
}

} // namespace cupid::n64
