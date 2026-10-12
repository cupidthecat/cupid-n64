#pragma once

#include "../../support/test.hpp"
#include "core/debug/inspection.hpp"
#include "core/state/core_state.hpp"
#include "desktop/debug/format.hpp"
#include <memory>

namespace tracing_test {

using namespace cupid::n64;
using test::equal;
constexpr auto all_traces = (trace_bit(TraceKind::Tlb) << 1) - 1;

inline std::unique_ptr<Console> machine() {
  ConsoleConfig config;
  config.random_seed = 132;
  config.rtc_clock = [] { return std::int64_t(946684800); };
  config.disk_clock = config.rtc_clock;
  auto result = std::make_unique<Console>(config);
  std::vector<std::uint8_t> cartridge(4096), pif(0x7c0);
  cartridge[0] = 0x80;
  cartridge[1] = 0x37;
  cartridge[2] = 0x12;
  cartridge[3] = 0x40;
  equal(result->load(cartridge, pif), true);
  result->power();
  result->write(0x04700008, 4, 0);
  result->write(0x0470000c, 4, 0x14);
  result->write(0x04300000, 4, 0x10f);
  result->write(0x03f80008, 4, 0x00080008);
  for (unsigned n = 0; n < 4; ++n) {
    result->write(0x03f0000c, 4, 0x02000000);
    result->write(0x03f00004, 4, (n + 4) * 2 << 26);
  }
  for (unsigned n = 0; n < 4; ++n)
    result->write(0x03f00004 + (n + 4) * 0x800, 4, n * 2 << 26);
  equal(result->ram().identity(), true);
  result->cpu().write_control(Status, 0x30000000);
  result->cpu().set_pc(0xffffffff80001000);
  return result;
}

inline void code(Console &console, std::span<const std::uint32_t> words) {
  std::vector<std::uint8_t> bytes;
  for (auto word : words)
    for (unsigned n = 0; n < 4; ++n)
      bytes.push_back(static_cast<std::uint8_t>(word >> (24 - n * 8)));
  Inspection::write(console, MemoryRegionId::Rdram, 0x1000, bytes);
}

inline void raw_word(Console &console, std::uint32_t address, std::uint32_t word) {
  const std::array<std::uint8_t, 4> bytes{
      static_cast<std::uint8_t>(word >> 24), static_cast<std::uint8_t>(word >> 16),
      static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word)};
  Inspection::write(console, MemoryRegionId::Rdram, address, bytes);
}

void instruction_tests();
void execution_tests();

} // namespace tracing_test
