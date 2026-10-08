#include "native_memory_fixture.hpp"

namespace test {
using namespace cupid::n64;
namespace {

void mapping(Cpu &cpu, unsigned flags) {
  cpu.write_control(Index, 0);
  cpu.write_control(EntryHi, 0x4000);
  cpu.write_control(EntryLo0, (0x2000 >> 6) | flags | 0x10);
  cpu.write_control(EntryLo1, (0x3000 >> 6) | flags | 0x10);
  cpu.execute(co(2));
}

void fault(Cpu &cpu, unsigned exception, std::uint64_t vector, std::uint64_t address,
           std::uint64_t pc) {
  equal((cpu.read_control(Cause) >> 2) & 31, exception);
  equal(cpu.read_control(BadVAddr), address);
  equal(cpu.read_control(Epc), pc);
  equal(cpu.state().pc, vector);
  equal(cpu.state().gpr[2], 0x1122334455667788);
  equal(cpu.read_control(LlAddr), 0);
}

} // namespace

void linked_load_tests() {
  for (unsigned status : {0x30000000u, 0x30000080u, 0x30000048u, 0x30000030u}) {
    for (bool present : {false, true}) {
      for (unsigned offset = 0; offset < 8; ++offset) {
        Fixture f;
        if (present)
          mapping(f.cpu, 0);
        f.cpu.write_control(Status, status);
        f.cpu.set_pc(0xffffffffa0001000);
        f.cpu.state().gpr[1] = 0x4000;
        f.cpu.state().gpr[2] = 0x1122334455667788;
        const bool word_aligned = !(offset & 3);
        const auto vector =
            word_aligned && !present
                ? (status == 0x30000000 ? 0xffffffff80000000ull : 0xffffffff80000080ull)
                : 0xffffffff80000180ull;
        f.cpu.execute(i(0x34, 1, 2, static_cast<std::uint16_t>(offset)));
        fault(f.cpu, word_aligned ? 2 : 4, vector, 0x4000 + offset, 0xffffffffa0001000);
        equal(f.memory.transfers.size(), 0);
      }
    }
  }
  for (unsigned flags : {2u, 6u, 7u}) {
    Fixture f;
    mapping(f.cpu, flags);
    f.cpu.set_pc(0xffffffffa0001000);
    f.cpu.state().gpr[1] = 0x4000;
    f.cpu.state().gpr[2] = 0x1122334455667788;
    f.cpu.execute(i(0x34, 1, 2, 4));
    fault(f.cpu, 4, 0xffffffff80000180, 0x4004, 0xffffffffa0001000);
    equal(f.memory.transfers.size(), 0);
  }
  for (bool interpreted : {false, true}) {
    for (bool present : {false, true}) {
      for (bool delay : {false, true}) {
        native_memory::CachedFixture f;
        if (present)
          mapping(f.cpu, 0);
        f.cpu.set_pc(0xffffffff80001000);
        f.cpu.state().gpr[1] = 0x4000;
        f.cpu.state().gpr[2] = 0x1122334455667788;
        f.code(0, delay ? i(4, 0, 0, 3) : i(0x34, 1, 2, 4));
        f.code(4, delay ? i(0x34, 1, 2, 4) : c(4, 0, Status));
        const auto target = f.cpu.state().clocks + 4096;
        equal(interpreted ? f.cpu.run_interpreted_block(target) : f.cpu.run_block(target), true);
        fault(f.cpu, 2, present ? 0xffffffff80000180 : 0xffffffff80000000, 0x4004,
              0xffffffff80001000);
        equal(f.cpu.read_control(Cause) >> 31, delay);
        equal(f.memory.transfers.size(), 1);
      }
    }
  }
  {
    Fixture f;
    mapping(f.cpu, 6);
    f.memory.put(0x2000, 8, 0x89abcdef01234567);
    f.cpu.state().gpr[1] = 0x4000;
    f.cpu.execute(i(0x34, 1, 2, 0));
    equal(f.cpu.state().gpr[2], 0x89abcdef01234567);
    equal(f.cpu.read_control(LlAddr), 0x200);
    f.cpu.state().gpr[2] = 0x76543210fedcba98;
    f.cpu.execute(i(0x3c, 1, 2, 0));
    equal(f.cpu.state().gpr[2], 1);
    equal(f.memory.get(0x2000, 8), 0x76543210fedcba98);
  }
}

} // namespace test
