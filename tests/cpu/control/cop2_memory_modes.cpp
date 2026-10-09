#include "../native_memory_fixture.hpp"

namespace test {
using namespace cupid::n64;

void cop2_memory_modes_tests() {
  constexpr std::uint64_t latch = 0x8123456789abcdefull;
  constexpr std::uint64_t payload = 0xfedcba9876543210ull;
  constexpr std::uint64_t saved_epc = 0x12345678;
  constexpr std::uint64_t start = 0xffffffff80001000ull;
  for (unsigned route = 0; route < 4; ++route)
    for (bool enabled : {false, true})
      for (unsigned operation : {50u, 54u, 58u, 62u})
        for (unsigned selector : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 16u})
          for (bool delay : {false, true}) {
            native_memory::CachedFixture f;
            f.cpu.write_control(Status, 0x70000000);
            f.cpu.state().gpr[7] = latch;
            f.cpu.execute(i(18, 5, 7, 0));
            const auto status = enabled ? 0x70000000u : 0x30000000u;
            f.cpu.write_control(Status, status);
            f.cpu.write_control(Epc, saved_epc);
            f.cpu.state().gpr[2] = payload;
            f.cpu.state().gpr[26] = start + 0x200;
            f.cpu.set_pc(start);
            const auto instruction = i(operation, selector, 2, 4);
            f.code(0, delay ? i(4, 0, 0, 8) : instruction);
            f.code(4, delay ? instruction : r(8, 26, 0, 0));
            f.code(8, 0);
            auto registers = f.cpu.state().gpr;
            const auto clocks = f.cpu.state().clocks;
            if (route == 0) {
              if (delay)
                f.cpu.execute(i(4, 0, 0, 8));
              f.cpu.execute(instruction);
            } else if (route == 1) {
              f.cpu.step();
              if (delay)
                f.cpu.step();
            } else {
              equal(route == 2 ? f.cpu.run_block(clocks) : f.cpu.run_interpreted_block(clocks),
                    true);
            }
            const bool transfer =
                route < 2 && enabled && (selector <= 2 || (selector >= 4 && selector <= 6));
            const bool read = transfer && selector <= 2;
            const bool write = transfer && selector >= 4;
            if (read)
              registers[2] = selector == 1 ? latch : 0xffffffff89abcdefull;
            equal(f.cpu.state().pc, transfer ? start + (delay ? 36 : 4) : 0xffffffff80000180ull);
            equal(f.cpu.read_control(Cause), transfer ? 0
                                                      : 0x20000000ull | (enabled ? 10u : 11u) << 2 |
                                                            (delay ? 0x80000000ull : 0));
            equal(f.cpu.read_control(Status), transfer ? status : status | 2);
            equal(f.cpu.read_control(Epc), transfer ? saved_epc : start);
            equal(f.cpu.read_control(BadVAddr), 0);
            equal(f.cpu.in_delay_slot(), false);
            equal(f.cpu.state().clocks, clocks + (route ? 96 : 0) + (delay ? 4 : 2));
            for (unsigned reg = 0; reg < registers.size(); ++reg)
              equal(f.cpu.state().gpr[reg], registers[reg]);
            equal(f.memory.transfers.size(), route ? 1 : 0);
            for (const auto &transaction : f.memory.transfers) {
              equal(transaction.address, 0x1000);
              equal(transaction.bytes, 32);
              equal(transaction.store, false);
            }
            f.cpu.write_control(Status, 0x70000000);
            f.cpu.set_pc(0xffffffffa0003000ull);
            f.cpu.execute(i(18, 1, 8, 0));
            equal(f.cpu.state().gpr[8], write ? payload : latch);
          }
}
} // namespace test
