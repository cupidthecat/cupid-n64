#include "core/cpu/cpu.hpp"
#include "core/debug/diagnostics.hpp"
#include <algorithm>

namespace cupid::n64 {

Diagnostics::CacheObservation Diagnostics::observe_cache(std::uint32_t address) const noexcept {
  CacheObservation result;
  if (!cpu_)
    return result;
  const auto first = (address & 0xfff) >> 4;
  for (const auto index : {first, first + 256}) {
    const auto &line = cpu_->dcache_[index];
    if (!line.hit(address))
      continue;
    const auto &context = cache_[index];
    const bool known = context.known && context.address == (address & ~15u);
    if (!result.valid)
      result.fill_pc = known ? context.fill_pc : 0;
    result.valid = true;
    if (!line.dirty)
      continue;
    const auto dirty = known ? context.dirty : std::uint16_t(0xffff);
    if (dirty) {
      result.dirty |= dirty;
      result.fill_pc = known ? context.fill_pc : 0;
      result.write_pc = known ? context.write_pc : 0;
    }
  }
  return result;
}

void Diagnostics::cache_fill(unsigned index, std::uint32_t address, std::uint64_t pc) noexcept {
  if (coherence() && index < cache_.size())
    cache_[index] = {.address = address & ~15u, .known = true, .fill_pc = pc};
}

void Diagnostics::cache_dirty(unsigned index, std::uint32_t address, std::uint16_t mask,
                              std::uint64_t pc) noexcept {
  if (!coherence() || index >= cache_.size())
    return;
  auto &context = cache_[index];
  if (!context.known || context.address != (address & ~15u))
    context = {.address = address & ~15u,
               .dirty = static_cast<std::uint16_t>(cpu_ && cpu_->dcache_[index].dirty ? 0xffff : 0),
               .known = true};
  context.dirty |= mask;
  context.write_pc = pc;
}

void Diagnostics::cache_clean(unsigned index) noexcept {
  if (index < cache_.size())
    cache_[index].dirty = 0;
}

void Diagnostics::memory_access(MemoryRequestor requestor, std::uint32_t address, unsigned bytes,
                                bool write) noexcept {
  if (!coherence() || requestor == MemoryRequestor::Host ||
      (write &&
       (requestor == MemoryRequestor::InstructionCache || requestor == MemoryRequestor::DataCache)))
    return;
  for (unsigned n = 0; n < bytes;) {
    const auto current = address + n;
    const auto length = std::min(bytes - n, 16 - (current & 15));
    const auto observation = observe_cache(current);
    const auto mask = static_cast<std::uint16_t>(((1u << length) - 1) << (current & 15));
    if ((write && observation.valid) ||
        (!write && requestor != MemoryRequestor::SignalDma && (observation.dirty & mask)))
      record({.kind = write ? DiagnosticKind::CachedWrite : DiagnosticKind::DirtyRead,
              .device = DiagnosticDevice::Rdram,
              .requestor = requestor,
              .origin = origin(),
              .dma = dma_origin(requestor),
              .fill_pc = observation.fill_pc,
              .write_pc = observation.write_pc,
              .address = current,
              .bytes = length});
    n += length;
  }
}

void Diagnostics::dma_to_local(std::uint32_t dram, std::uint32_t local, unsigned bytes) noexcept {
  if (!coherence() || !local_)
    return;
  for (unsigned n = 0; n < bytes; ++n) {
    const auto address = dram + n;
    const auto observation = observe_cache(address);
    const bool dirty = (observation.dirty & (1u << (address & 15))) != 0;
    if (local & 0x1000) {
      if (dirty)
        record({.kind = DiagnosticKind::DirtyRead,
                .device = DiagnosticDevice::Rsp,
                .requestor = MemoryRequestor::SignalDma,
                .origin = origin(),
                .dma = dma_origin(MemoryRequestor::SignalDma),
                .fill_pc = observation.fill_pc,
                .write_pc = observation.write_pc,
                .address = address,
                .related_address = 0x1000 | ((local + n) & 0xfff),
                .bytes = 1});
      continue;
    }
    (*local_)[(local + n) & 0xfff] = {.dma = dma_origin(MemoryRequestor::SignalDma),
                                      .dram = address,
                                      .fill_pc = observation.fill_pc,
                                      .write_pc = observation.write_pc,
                                      .dirty = dirty};
  }
}

void Diagnostics::local_access(std::uint32_t address, unsigned bytes, bool write,
                               MemoryRequestor requestor) noexcept {
  if (!coherence() || !local_ || (address & 0x1000))
    return;
  for (unsigned n = 0; n < bytes; ++n) {
    const auto current = (address + n) & 0xfff;
    auto &context = (*local_)[current];
    if (!write && context.dirty)
      record({.kind = DiagnosticKind::DmemRead,
              .device = DiagnosticDevice::Rsp,
              .requestor = requestor,
              .origin = origin(),
              .dma = context.dma,
              .fill_pc = context.fill_pc,
              .write_pc = context.write_pc,
              .address = current,
              .related_address = context.dram,
              .bytes = 1});
    context.dirty = false;
  }
}

void Diagnostics::check_refresh(std::uint32_t refresh) noexcept {
  if (!homebrew_ || refresh_warned_ || (refresh & (1u << 17)))
    return;
  refresh_warned_ = true;
  record({.kind = DiagnosticKind::RefreshDisabled,
          .device = DiagnosticDevice::Ri,
          .origin = origin(),
          .address = 0x04700010,
          .bytes = 4});
}

void Diagnostics::exception(unsigned code, bool delay_slot) noexcept {
  if (homebrew_ && code < 64 && (exception_mask_ & (1ull << code)))
    record(
        {.kind = DiagnosticKind::Exception, .origin = origin(), .value = delay_slot, .code = code});
}

} // namespace cupid::n64
