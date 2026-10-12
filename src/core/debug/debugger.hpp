#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cupid::n64 {

class Console;

enum class DebugStopKind {
  Attached,
  Interrupt,
  Step,
  Breakpoint,
  ReadWatch,
  WriteWatch,
  AccessWatch
};

struct DebugStop {
  DebugStopKind kind = DebugStopKind::Attached;
  std::uint64_t pc = 0;
  std::uint64_t address = 0;
  bool operator==(const DebugStop &) const = default;
};

enum class DebugPointKind { Software, Hardware, Write, Read, Access };

class Debugger {
public:
  static constexpr unsigned register_count = 71;
  static constexpr unsigned maximum_points = 128;
  static constexpr unsigned maximum_transfer = 4096;
  explicit Debugger(Console &console);
  ~Debugger();
  Debugger(const Debugger &) = delete;
  Debugger &operator=(const Debugger &) = delete;
  void attach();
  void detach();
  bool attached() const {
    return attached_;
  }
  bool halted() const {
    return halted_;
  }
  const DebugStop &stop() const {
    return stop_;
  }
  void interrupt();
  void resume(bool step = false, std::optional<std::uint64_t> pc = {});
  void advance(unsigned clocks = 4096);
  std::optional<std::uint64_t> read_register(unsigned index) const;
  bool write_register(unsigned index, std::uint64_t value);
  std::optional<std::vector<std::uint8_t>> read_memory(std::uint64_t address, unsigned length);
  bool write_memory(std::uint64_t address, std::span<const std::uint8_t> bytes);
  bool point(DebugPointKind kind, std::uint64_t address, unsigned length, bool insert);
  static std::uint64_t canonical(std::uint64_t address);

private:
  friend class Cpu;
  struct Point {
    DebugPointKind kind;
    std::uint64_t address;
    unsigned length;
    bool operator==(const Point &) const = default;
  };
  void memory_access(std::uint64_t address, unsigned bytes, bool write);
  void halt(DebugStopKind kind, std::uint64_t address = 0);
  std::optional<std::uint32_t> io_word(std::uint32_t physical) const;
  Console &console_;
  std::vector<Point> points_;
  DebugStop stop_;
  std::optional<std::uint64_t> skip_breakpoint_;
  bool attached_ = false, halted_ = false, stepping_ = false;
};

} // namespace cupid::n64
