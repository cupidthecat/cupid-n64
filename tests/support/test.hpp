#pragma once

#include "core/cpu/cpu.hpp"
#include <iostream>
#include <source_location>
#include <unordered_map>
#include <vector>

namespace test {

inline unsigned checks = 0;
inline unsigned failures = 0;

inline void equal(std::uint64_t actual, std::uint64_t expected,
                  std::source_location location = std::source_location::current()) {
  ++checks;
  if (actual == expected)
    return;
  ++failures;
  std::cerr << location.file_name() << ':' << location.line() << " expected 0x" << std::hex
            << expected << ", got 0x" << actual << std::dec << '\n';
}

struct Transfer {
  std::uint32_t address;
  unsigned bytes;
  bool store;
  std::uint64_t value;
};

class Memory : public cupid::n64::Bus {
public:
  std::unordered_map<std::uint32_t, std::uint8_t> data;
  std::vector<Transfer> transfers;
  std::uint32_t clocks = 0;
  bool success = true;

  void put(std::uint32_t address, unsigned bytes, std::uint64_t value) {
    address &= ~(bytes - 1u);
    for (unsigned i = 0; i < bytes; ++i)
      data[address + i] = static_cast<std::uint8_t>(value >> ((bytes - 1 - i) * 8));
  }

  std::uint64_t get(std::uint32_t address, unsigned bytes) {
    address &= ~(bytes - 1u);
    std::uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i)
      value = (value << 8) | data[address + i];
    return value;
  }

  cupid::n64::BusRead read(std::uint32_t address, unsigned bytes) override {
    const auto value = get(address, bytes);
    transfers.push_back({address, bytes, false, value});
    return {value, clocks, success};
  }

  cupid::n64::BusWrite write(std::uint32_t address, unsigned bytes, std::uint64_t value) override {
    transfers.push_back({address, bytes, true, value});
    if (success)
      put(address, bytes, value);
    return {clocks, success};
  }
};

constexpr std::uint32_t r(unsigned function, unsigned rs, unsigned rt, unsigned rd,
                          unsigned shift = 0) {
  return (rs << 21) | (rt << 16) | (rd << 11) | (shift << 6) | function;
}

constexpr std::uint32_t i(unsigned operation, unsigned rs, unsigned rt, std::uint16_t immediate) {
  return (operation << 26) | (rs << 21) | (rt << 16) | immediate;
}

constexpr std::uint32_t c(unsigned function, unsigned rt, unsigned rd) {
  return (0x10u << 26) | (function << 21) | (rt << 16) | (rd << 11);
}

constexpr std::uint32_t co(unsigned function) {
  return 0x42000000 | function;
}

struct Fixture {
  Memory memory;
  cupid::n64::Cpu cpu{memory};
  Fixture() {
    cpu.write_control(cupid::n64::Status, 0x30000000);
    cpu.set_pc(0xffffffffa0001000);
  }
  void code(unsigned offset, std::uint32_t instruction) {
    memory.put(0x1000 + offset, 4, instruction);
  }
  unsigned exception() {
    return static_cast<unsigned>((cpu.read_control(cupid::n64::Cause) >> 2) & 31);
  }
};

void integer_tests();
void memory_tests();
void control_tests();
void cache_tests();
void execution_tests();
void fpu_tests();

} // namespace test
