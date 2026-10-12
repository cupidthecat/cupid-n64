#pragma once

#include <cstdint>
#include <vector>

namespace cupid::n64 {

enum class DebugProcessor { Cpu, Rsp };
enum class TraceKind { CpuInstruction, RspInstruction, Exception, Interrupt, Nmi, Tlb };
enum class TlbEvent { Load, Store, Write, Invalid, Miss, Modification };

constexpr std::uint32_t trace_bit(TraceKind kind) {
  const auto index = static_cast<unsigned>(kind);
  return index < 32 ? 1u << index : 0;
}

struct TraceAddressRange {
  std::uint64_t first = 0, last = ~0ull;
};

struct TraceConfig {
  std::uint32_t mask = 0;
  unsigned capacity = 4096;
  TraceAddressRange cpu, rsp{0, 0xfff};
};

struct TraceRecord {
  TraceKind kind = TraceKind::CpuInstruction;
  TlbEvent tlb = TlbEvent::Load;
  std::uint64_t sequence = 0, clock = 0, pc = 0, address = 0, value = 0;
  std::uint32_t instruction = 0, code = 0, index = 0, mask = 0, lo0 = 0, lo1 = 0;
  bool delay_slot = false;
  bool operator==(const TraceRecord &) const = default;
};

// The owner configures and reads this host-only log between execution intervals.
class TraceLog {
public:
  static constexpr unsigned maximum_records = 65536;
  void configure(TraceConfig config);
  void set_enabled(TraceKind kind, bool enabled);
  const TraceConfig &config() const {
    return config_;
  }
  bool enabled(TraceKind kind) const noexcept {
    return (config_.mask & trace_bit(kind)) != 0;
  }
  void record(TraceRecord value) noexcept;
  void clear() noexcept;
  void stop() noexcept {
    config_.mask = 0;
  }
  std::vector<TraceRecord> records() const;
  std::uint64_t dropped() const {
    return dropped_;
  }

private:
  TraceConfig config_;
  std::vector<TraceRecord> storage_;
  unsigned first_ = 0, count_ = 0;
  std::uint64_t sequence_ = 0, dropped_ = 0;
};

} // namespace cupid::n64
