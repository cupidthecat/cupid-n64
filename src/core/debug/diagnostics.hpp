#pragma once

#include "core/debug/trace.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cupid::n64 {

class Console;
class Cpu;

enum class MemoryRequestor {
  InstructionCache,
  DataCache,
  CpuUncached,
  SignalDma,
  PeripheralDma,
  SerialDma,
  DisplayDraw,
  AudioDma,
  VideoDma,
  DisplayDma,
  Count,
  Host
};

enum class DiagnosticDevice { Mi, Vi, Audio, Pi, Si, Ri, Rdram, Pif, Rdp, Rsp, Cartridge, Disk };

constexpr std::uint32_t diagnostic_bit(DiagnosticDevice device) {
  const auto index = static_cast<unsigned>(device);
  return index < 32 ? 1u << index : 0;
}

struct DiagnosticConfig {
  bool profiling = false;
  bool coherence = false;
  std::uint32_t devices = 0;
  unsigned capacity = 2048;
  std::uint32_t first_address = 0, last_address = ~0u;
  TraceAddressRange cpu{}, rsp{0, 0xfff};
};

struct DiagnosticOrigin {
  DebugProcessor processor = DebugProcessor::Cpu;
  std::uint64_t pc = 0, clock = 0;
  bool known = false;
  bool operator==(const DiagnosticOrigin &) const = default;
};

enum class DiagnosticKind {
  RegisterRead,
  RegisterWrite,
  DirtyRead,
  CachedWrite,
  DmemRead,
  RefreshDisabled,
  HomebrewLog,
  Exception,
  HostControl
};

struct DiagnosticRecord {
  DiagnosticKind kind = DiagnosticKind::RegisterRead;
  DiagnosticDevice device = DiagnosticDevice::Mi;
  MemoryRequestor requestor = MemoryRequestor::Host;
  DiagnosticOrigin origin{}, dma{};
  std::uint64_t sequence = 0, value = 0, fill_pc = 0, write_pc = 0;
  std::uint32_t address = 0, related_address = 0, bytes = 0, code = 0;
  std::string text{};
};

struct TrafficMetric {
  std::uint64_t reads = 0, writes = 0;
  bool operator==(const TrafficMetric &) const = default;
};

struct ProfileSnapshot {
  // CPU cycles use Count ticks. RSP totals use its pipeline clock units (three per cycle).
  std::array<std::uint64_t, 8> cpu{};
  std::array<std::uint64_t, 2> rsp{};
  // Traffic counts transferred bytes, excluding host inspection and compilation.
  std::array<TrafficMetric, static_cast<unsigned>(MemoryRequestor::Count)> traffic{};
  std::uint64_t read(unsigned code) const;
  ProfileSnapshot &operator+=(const ProfileSnapshot &other);
  ProfileSnapshot &operator-=(const ProfileSnapshot &other);
  bool operator==(const ProfileSnapshot &) const = default;
};

enum class HostControl { None, Exit, FastForward, NormalSpeed };

// Host diagnostics are configured between execution intervals and stay outside guest state.
class Diagnostics {
public:
  static constexpr unsigned maximum_records = 65536;
  static constexpr unsigned maximum_text = 4096;
  void connect(Console &console, Cpu &cpu, TraceLog &trace);
  void configure(DiagnosticConfig config);
  const DiagnosticConfig &config() const {
    return config_;
  }
  void set_homebrew(bool enabled);
  void stop();
  void connect_memory_fence(std::function<void()> fence) {
    memory_fence_ = std::move(fence);
  }
  bool homebrew() const {
    return homebrew_;
  }
  bool profiling() const {
    return homebrew_ || config_.profiling;
  }
  bool coherence() const {
    return homebrew_ || config_.coherence;
  }
  bool instrumented() const {
    return profiling() || coherence();
  }
  void power();
  void clear_log() noexcept;
  void clear_profiles() noexcept;
  void reset_context() noexcept;
  std::vector<DiagnosticRecord> records() const;
  std::uint64_t dropped() const {
    return dropped_;
  }
  ProfileSnapshot profile() const noexcept;
  void synchronize_cpu(bool discard_remainder = false) noexcept;
  void cpu_cycles(std::uint64_t ticks, bool exception) noexcept;
  void cache_access(bool instruction, bool hit) noexcept;
  void cache_writeback(bool instruction) noexcept;
  void rsp_cycles(std::uint64_t clocks, bool halted) noexcept;
  void traffic(MemoryRequestor requestor, unsigned bytes, bool write) noexcept;
  void register_access(DiagnosticDevice device, std::uint32_t address, std::uint64_t value,
                       bool write, unsigned bytes = 4) noexcept;
  DiagnosticOrigin origin() const noexcept;
  void set_origin(DiagnosticOrigin origin) noexcept {
    origin_ = origin;
  }
  DiagnosticOrigin explicit_origin() const noexcept {
    return origin_;
  }
  void set_dma_origin(MemoryRequestor requestor, DiagnosticOrigin origin) noexcept;
  DiagnosticOrigin dma_origin(MemoryRequestor requestor) const noexcept;
  void cache_fill(unsigned index, std::uint32_t address, std::uint64_t pc) noexcept;
  void cache_dirty(unsigned index, std::uint32_t address, std::uint16_t mask,
                   std::uint64_t pc) noexcept;
  void cache_clean(unsigned index) noexcept;
  void memory_access(MemoryRequestor requestor, std::uint32_t address, unsigned bytes,
                     bool write) noexcept;
  void dma_to_local(std::uint32_t dram, std::uint32_t local, unsigned bytes) noexcept;
  void local_access(std::uint32_t address, unsigned bytes, bool write,
                    MemoryRequestor requestor = MemoryRequestor::SignalDma) noexcept;
  void check_refresh(std::uint32_t refresh) noexcept;
  void exception(unsigned code, bool delay_slot) noexcept;
  void rsp_instruction() noexcept;
  bool cpu_extension(std::uint32_t instruction);
  bool rsp_extension(std::uint32_t instruction);
  HostControl control() const {
    return control_;
  }
  HostControl take_control() noexcept;

private:
  std::optional<std::uint64_t> extension(DebugProcessor processor, std::uint32_t instruction,
                                         std::uint64_t rd, std::uint64_t rt);
  void memory_log(DebugProcessor processor, std::uint64_t address, std::uint64_t length,
                  unsigned code);
  void memory_dump(DebugProcessor processor, std::uint64_t address, std::uint64_t length);
  void register_log(std::uint32_t mask, unsigned code);
  struct CacheContext {
    std::uint32_t address = 0;
    std::uint16_t dirty = 0;
    bool known = false;
    std::uint64_t fill_pc = 0, write_pc = 0;
  };
  struct LocalContext {
    DiagnosticOrigin dma;
    std::uint32_t dram = 0;
    std::uint64_t fill_pc = 0, write_pc = 0;
    bool dirty = false;
  };
  struct CacheObservation {
    bool valid = false;
    std::uint16_t dirty = 0;
    std::uint64_t fill_pc = 0, write_pc = 0;
  };
  CacheObservation observe_cache(std::uint32_t address) const noexcept;
  bool accepts(DiagnosticOrigin origin) const noexcept;
  void record(DiagnosticRecord record) noexcept;
  void text(DebugProcessor processor, std::string message);
  void profile_command(std::uint64_t slot, unsigned command);
  std::uint64_t profile_read(std::uint64_t slot, unsigned code) const;
  void request_control(unsigned code);
  DiagnosticConfig config_;
  Console *console_ = nullptr;
  Cpu *cpu_ = nullptr;
  TraceLog *trace_ = nullptr;
  bool homebrew_ = false, refresh_warned_ = false;
  DiagnosticOrigin origin_;
  std::array<DiagnosticOrigin, static_cast<unsigned>(MemoryRequestor::Count)> dma_origins_{};
  std::unique_ptr<std::array<CacheContext, 512>> cache_;
  std::unique_ptr<std::array<LocalContext, 4096>> local_;
  ProfileSnapshot profile_;
  std::uint64_t cpu_profile_clock_ = 0;
  std::vector<ProfileSnapshot> slots_;
  std::uint64_t exception_mask_ = 0;
  unsigned trace_countdown_ = 0;
  bool emux_trace_ = false, previous_rsp_trace_ = false;
  HostControl control_ = HostControl::None;
  std::function<void()> memory_fence_;
  std::vector<DiagnosticRecord> storage_;
  unsigned first_ = 0, count_ = 0;
  std::uint64_t sequence_ = 0, dropped_ = 0;
};

class DiagnosticScope {
public:
  DiagnosticScope(Diagnostics *diagnostics, DiagnosticOrigin origin)
      : diagnostics_(diagnostics),
        previous_(diagnostics ? diagnostics->explicit_origin() : DiagnosticOrigin{}) {
    if (diagnostics_)
      diagnostics_->set_origin(origin);
  }
  ~DiagnosticScope() {
    if (diagnostics_)
      diagnostics_->set_origin(previous_);
  }

private:
  Diagnostics *diagnostics_;
  DiagnosticOrigin previous_;
};

} // namespace cupid::n64
