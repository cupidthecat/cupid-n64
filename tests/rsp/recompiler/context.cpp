#include "../fixture.hpp"
#include <utility>
#include <vector>

namespace test {
using namespace cupid::n64;
namespace {

void compare_dispatch(RspFixture &actual, RspFixture &expected) {
  equal(actual.rsp.pc(), expected.rsp.pc());
  equal(actual.rsp.clocks(), expected.rsp.clocks());
  const auto &a = actual.rsp.state();
  const auto &b = expected.rsp.state();
  for (unsigned reg = 0; reg < 32; ++reg) {
    equal(a.gpr[reg], b.gpr[reg]);
    for (unsigned lane = 0; lane < 8; ++lane)
      equal(a.vectors[reg].lanes[lane], b.vectors[reg].lanes[lane]);
  }
  for (unsigned lane = 0; lane < 8; ++lane)
    equal(a.accumulator.get(lane), b.accumulator.get(lane));
  equal(a.carry_low, b.carry_low);
  equal(a.carry_high, b.carry_high);
  equal(a.compare_low, b.compare_low);
  equal(a.compare_high, b.compare_high);
  equal(a.extension, b.extension);
  equal(a.divide_input, b.divide_input);
  equal(a.divide_output, b.divide_output);
  equal(a.divide_double, b.divide_double);
  equal(actual.rsp.dma_clocks(), expected.rsp.dma_clocks());
  equal(actual.rsp.status().semaphore, expected.rsp.status().semaphore);
  for (unsigned address = 0; address < 28; address += 4)
    equal(actual.rsp.read_io(address), expected.rsp.read_io(address));
  equal(actual.mi.read_word(8), expected.mi.read_word(8));
  for (unsigned address = 0; address < 8192; address += 4)
    equal(actual.rsp.read_local(address, 4), expected.rsp.read_local(address, 4));
  for (unsigned address = 0; address < 64; address += 4)
    equal(actual.ram.read(address, 4), expected.ram.read(address, 4));
}

} // namespace

void rsp_context_tests() {
  for (unsigned reg = 1; reg < 31; ++reg) {
    RspFixture f;
    const auto instruction = i(9, reg, 31, 1);
    f.rsp.write_local(0x1000, 4, instruction);
    f.rsp.write_local(0x1004, 4, 0x08000000);
    f.rsp.write_local(0x1008, 4, i(35, 0, reg, 0));
    f.rsp.write_local(0, 4, 41);
    f.rsp.write_io(16, 1);
    for (unsigned stage = 0; stage < 5; ++stage) {
      if (stage == 2)
        f.rsp.write_local(0x1000, 4, instruction);
      if (stage == 3)
        f.rsp.write_local(0x1000, 4, instruction + 1);
      f.rsp.advance(static_cast<std::uint32_t>(f.rsp.clocks() + 1));
      equal(f.rsp.clocks(), stage < 2 ? 11 : 14);
      equal(f.rsp.pc(), 0);
      equal(f.rsp.state().gpr[31], stage == 0 ? 1 : stage < 3 ? 42 : 43);
    }
  }
  for (unsigned mode = 0; mode < 4; ++mode)
    for (bool exposed : {false, true}) {
      RspFixture actual, expected;
      std::array<std::vector<std::pair<std::uint32_t, unsigned>>, 2> invalidations;
      std::array fixtures{&actual, &expected};
      std::array<std::span<std::uint8_t, 4096>, 2> memory{actual.rsp.dmem(), expected.rsp.dmem()};
      for (unsigned n = 0; n < fixtures.size(); ++n) {
        auto &f = *fixtures[n];
        f.initialize();
        f.rsp.write_local(0x1000, 4, i(9, 1, 1, 1));
        f.rsp.write_local(0x1004, 4, 0x08000000);
        f.rsp.write_local(0x1008, 4, 0);
        if (mode == 2) {
          f.rsp.write_local(0x1000, 4, 0x08000004);
          f.rsp.write_local(0x1004, 4, 0x08000000);
          f.rsp.write_local(0x1010, 4, i(9, 1, 1, 1));
        }
        if (mode == 3) {
          f.rsp.state().gpr[2] = 2;
          f.rsp.write_local(0x1004, 4, c(4, 2, 4));
          f.rsp.write_local(0x1008, 4, 13);
        }
        if (exposed)
          memory[n] = f.rsp.imem();
        f.rsp.write_io(16, 1 | (1 << 8));
        f.rsp.advance(1);
        f.rsp.connect_invalidation([&, n](std::uint32_t address, unsigned bytes) {
          invalidations[n].emplace_back(address, bytes);
        });
        if (mode == 1) {
          f.ram.write(0, 4, i(9, 1, 1, 7));
          f.ram.write(4, 4, 0x08000000);
          f.ram.write(16, 8, 0);
          f.ram.write(32, 8, 0x123456789abcdef0ull);
          f.rsp.write_io(0, 0x1000);
          f.rsp.write_io(4, 0);
          f.rsp.write_io(8, (1 << 12) | (8 << 20));
          f.rsp.write_io(0, 0x1020);
          f.rsp.write_io(4, 32);
          f.rsp.write_io(8, 0);
        }
      }
      for (unsigned clocks : {2u, 3u, 17u, 128u, 4096u, 65535u}) {
        actual.rsp.advance(clocks);
        for (unsigned n = 0; n < clocks; ++n)
          expected.rsp.advance(1);
        compare_dispatch(actual, expected);
        equal(invalidations[0] == invalidations[1], true);
        if (clocks == 17) {
          for (unsigned n = 0; n < fixtures.size(); ++n) {
            auto &f = *fixtures[n];
            if (mode < 2) {
              if (exposed)
                memory[n][3] = 9;
              else
                f.rsp.write_local(0x1000, 4, i(9, 1, 1, 9));
            }
            if (mode == 3) {
              f.rsp.state().gpr[2] = 0;
              f.rsp.write_io(16, 5);
            }
          }
        }
      }
    }
}

} // namespace test
