#pragma once

#include <array>
#include <bit>
#include <cstdint>

namespace cupid::n64 {

constexpr std::uint64_t sign_word(std::uint32_t value) {
  return static_cast<std::uint64_t>(static_cast<std::int64_t>(std::bit_cast<std::int32_t>(value)));
}

constexpr std::int64_t signed_value(std::uint64_t value) {
  return std::bit_cast<std::int64_t>(value);
}

enum class Exception : unsigned {
  Interrupt = 0,
  TlbModification = 1,
  TlbLoad = 2,
  TlbStore = 3,
  AddressLoad = 4,
  AddressStore = 5,
  BusInstruction = 6,
  BusData = 7,
  Syscall = 8,
  Breakpoint = 9,
  ReservedInstruction = 10,
  CoprocessorUnusable = 11,
  Overflow = 12,
  Trap = 13,
};

enum ControlRegister : unsigned {
  Index = 0,
  Random = 1,
  EntryLo0 = 2,
  EntryLo1 = 3,
  Context = 4,
  PageMask = 5,
  Wired = 6,
  BadVAddr = 8,
  Count = 9,
  EntryHi = 10,
  Compare = 11,
  Status = 12,
  Cause = 13,
  Epc = 14,
  PrId = 15,
  Config = 16,
  LlAddr = 17,
  WatchLo = 18,
  WatchHi = 19,
  XContext = 20,
  ParityError = 26,
  CacheError = 27,
  TagLo = 28,
  TagHi = 29,
  ErrorEpc = 30,
};

struct CpuState {
  std::array<std::uint64_t, 32> gpr{};
  std::uint64_t hi = 0;
  std::uint64_t lo = 0;
  std::uint64_t pc = 0;
  std::uint64_t clocks = 0;
};

struct TlbEntry {
  std::uint64_t hi = 0;
  std::array<std::uint32_t, 2> lo{};
  std::uint32_t mask = 0;
};

struct Address {
  std::uint32_t physical = 0;
  bool cached = false;
};

} // namespace cupid::n64
