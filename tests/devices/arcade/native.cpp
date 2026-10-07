#include "../fixture.hpp"
#include "core/system/console.hpp"
#include <limits>

namespace test {
using namespace cupid::n64;

void arcade_native_tests() {
  ConsoleConfig config;
  config.arcade_profile = ArcadeProfile::Standard;
  Console console(config);
  auto &cpu = console.cpu();
  cpu.write_control(Status, 0x30000080);
  cpu.state().gpr[12] = 0x30000080;
  console.write(0xc0001000, 4, i(9, 0, 1, 123));
  console.write(0xc0001004, 4, i(16, 4, 12, Status << 11));
  constexpr auto pc = 0x98000000c0001000ull;
  const auto target = std::numeric_limits<std::uint64_t>::max();
  cpu.set_pc(pc);
  equal(cpu.run_block(target), true);
  equal(cpu.state().gpr[1], 123);
  console.write(0xc0001000, 4, i(9, 0, 1, 456));
  cpu.state().gpr[1] = 0;
  cpu.set_pc(pc);
  if (!cpu.run_block(target))
    cpu.step();
  equal(cpu.state().gpr[1], 123);
  cpu.state().gpr[8] = pc;
  cpu.execute(i(47, 8, 0, 0));
  cpu.set_pc(pc);
  equal(cpu.run_block(target), true);
  equal(cpu.state().gpr[1], 456);
  equal(console.frozen(), false);
}

} // namespace test
