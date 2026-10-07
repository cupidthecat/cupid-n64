#include "fixture.hpp"

namespace test {
using namespace cupid::n64;

void rsp_native_memory_tests() {
  RspFixture actual;
  RspFixture expected;
  for (bool store : {false, true}) {
    for (unsigned operation = 0; operation < 32; ++operation) {
      for (unsigned element = 0; element < 16; ++element) {
        for (unsigned offset = 0; offset < 32; ++offset) {
          for (unsigned immediate : {0u, 63u, 64u, 127u}) {
            actual.rsp.power();
            expected.rsp.power();
            const auto source = (element + operation) % 3 == 0 ? 0u : 31u;
            const auto target = (element * 7 + operation) & 31;
            for (unsigned reg = 0; reg < 32; ++reg) {
              actual.rsp.state().gpr[reg] =
                  reg ? 0xa1234000 + (offset < 16 ? offset : 0xfe0 + offset) : 0;
              for (unsigned lane = 0; lane < 8; ++lane)
                actual.rsp.state().vectors[reg].lanes[lane] =
                    static_cast<std::uint16_t>(reg * 2347 + lane * 3571 + offset * 47 + 0x8143);
            }
            expected.rsp.state() = actual.rsp.state();
            for (unsigned address = 0; address < 4096; ++address) {
              const auto byte =
                  static_cast<std::uint8_t>(address * 73 + (address >> 4) * 29 + element);
              actual.rsp.dmem()[address] = expected.rsp.dmem()[address] = byte;
            }
            const auto instruction = vector_memory(store, operation, target, source, element,
                                                   static_cast<std::uint8_t>(immediate));
            for (auto *fixture : {&actual, &expected}) {
              fixture->rsp.write_local(0x1000, 4, instruction);
              fixture->rsp.write_local(0x1004, 4, vector(63, 0, 0, 0));
              fixture->rsp.write_local(0x1008, 4, 13);
              fixture->rsp.write_io(16, 1);
            }
            actual.rsp.advance(1);
            const auto clocks = expected.rsp.step() + expected.rsp.step();
            const auto before = failures;
            equal(actual.rsp.clocks(), clocks - 1);
            equal(actual.rsp.pc(), expected.rsp.pc());
            equal(actual.rsp.status().broken, true);
            for (unsigned reg = 0; reg < 32; ++reg) {
              equal(actual.rsp.state().gpr[reg], expected.rsp.state().gpr[reg]);
              for (unsigned lane = 0; lane < 8; ++lane)
                equal(actual.rsp.state().vectors[reg].lanes[lane],
                      expected.rsp.state().vectors[reg].lanes[lane]);
            }
            for (unsigned address = 0; address < 4096; address += 4)
              equal(actual.rsp.read_local(address, 4), expected.rsp.read_local(address, 4));
            if (failures != before) {
              std::cerr << "vector memory store=" << store << " operation=" << operation
                        << " element=" << element << " offset=" << offset
                        << " immediate=" << immediate << '\n';
              return;
            }
          }
        }
      }
    }
  }
}

} // namespace test
