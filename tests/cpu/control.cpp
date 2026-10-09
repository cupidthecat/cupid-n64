#include "../support/test.hpp"

namespace test {
using namespace cupid::n64;

static void map(Fixture &f, unsigned index, std::uint64_t virtual_address, std::uint32_t physical,
                unsigned flags = 0x16, std::uint32_t mask = 0) {
  f.cpu.write_control(Index, index);
  f.cpu.write_control(EntryHi, virtual_address);
  f.cpu.write_control(EntryLo0, (physical >> 6) | flags);
  f.cpu.write_control(EntryLo1, ((physical + ((mask | 0x1fff) + 1) / 2) >> 6) | flags);
  f.cpu.write_control(PageMask, mask);
  f.cpu.execute(co(2));
}

void control_tests() {
  {
    Memory memory;
    Cpu cpu(memory);
    equal(cpu.state().pc, 0xffffffffbfc00000);
    equal(cpu.state().gpr[29], 0xffffffffa4001ff0);
    equal(cpu.read_control(Status), 0x3450ff04);
    equal(cpu.read_control(PrId), 0x0b22);
    cpu.write_control(PrId, 0);
    equal(cpu.read_control(PrId), 0x0b22);
  }
  {
    Fixture f;
    f.cpu.write_control(Cause, 0xffffffff);
    equal(f.cpu.read_control(Cause), 0x300);
    f.cpu.write_control(EntryLo0, ~0ull);
    equal(f.cpu.read_control(EntryLo0), 0x3fffffff);
    f.cpu.write_control(7, 0xabcdef0123456789);
    equal(f.cpu.read_control(31), 0xabcdef0123456789);
    f.cpu.state().gpr[1] = 0x1234567880000000;
    f.cpu.execute(c(4, 1, Epc));
    equal(f.cpu.read_control(Epc), 0x1234567880000000);
    f.cpu.execute(c(0, 2, Epc));
    equal(f.cpu.state().gpr[2], 0xffffffff80000000);
    f.cpu.execute(c(1, 2, Epc));
    equal(f.cpu.state().gpr[2], 0x1234567880000000);
  }
  {
    Fixture f;
    f.cpu.write_control(Count, 0);
    f.cpu.write_control(Compare, 2);
    f.cpu.execute(0);
    equal(f.cpu.read_control(Count), 0);
    f.cpu.execute(0);
    equal(f.cpu.read_control(Count), 1);
    f.cpu.advance_clocks(4);
    equal(f.cpu.read_control(Count), 2);
    equal(f.cpu.read_control(Cause) & 0x8000, 0);
    f.cpu.synchronize_timer();
    equal(f.cpu.read_control(Cause) & 0x8000, 0x8000);
    f.cpu.write_control(Compare, 2);
    equal(f.cpu.read_control(Cause) & 0x8000, 0);
    f.cpu.advance_clocks(4);
    equal(f.cpu.read_control(Cause) & 0x8000, 0);
    f.cpu.write_control(Count, 0xffffffff);
    f.cpu.write_control(Compare, 0);
    f.cpu.advance_clocks(4);
    equal(f.cpu.read_control(Count), 0);
    f.cpu.synchronize_timer();
    equal(f.cpu.read_control(Cause) & 0x8000, 0x8000);
  }
  {
    Fixture f;
    f.cpu.write_control(Status, 0x401);
    f.cpu.set_interrupt(2, true);
    f.cpu.execute(i(9, 0, 1, 1));
    equal(f.cpu.state().gpr[1], 0);
    equal(f.cpu.state().pc, 0xffffffff80000180);
    equal(f.cpu.read_control(Epc), 0xffffffffa0001000);
    equal(f.exception(), 0);
    f.cpu.set_interrupt(2, false);
    f.cpu.execute(co(0x18));
    equal(f.cpu.state().pc, 0xffffffffa0001000);
    equal(f.cpu.read_control(Status) & 2, 0);
  }
  {
    Fixture f;
    f.cpu.execute(i(4, 0, 0, 3));
    f.cpu.write_control(Status, 0x401);
    f.cpu.set_interrupt(2, true);
    f.cpu.step();
    equal(f.cpu.read_control(Cause) & 0x80000000, 0x80000000);
    equal(f.cpu.read_control(Epc), 0xffffffffa0001000);
  }
  for (unsigned flags : {0u, 2u, 6u, 7u}) {
    Fixture f;
    map(f, 1, 0x00400011, 0x2000, flags | 0x10);
    f.memory.put(0x2000, 4, 0x12345678);
    f.cpu.state().gpr[1] = 0x00400000;
    f.cpu.execute(i(0x23, 1, 2, 0));
    equal(f.exception(), flags & 2 ? 0 : 2);
    if (flags & 2) {
      equal(f.cpu.state().gpr[2], 0x12345678);
      f.cpu.state().gpr[2] = 42;
      f.cpu.execute(i(0x2b, 1, 2, 0));
      equal(f.exception(), flags & 4 ? 0 : 1);
      equal(f.memory.get(0x2000, 4), flags & 4 ? 42 : 0x12345678);
    }
  }
  {
    Fixture f;
    map(f, 7, 0x00400017, 0x2000);
    f.cpu.write_control(EntryHi, 0x00400117);
    f.cpu.execute(co(8));
    equal(f.cpu.read_control(Index), 7);
    f.cpu.write_control(EntryHi, 0x00400018);
    f.cpu.execute(co(8));
    equal(f.cpu.read_control(Index), 0x80000000);
    f.cpu.write_control(Index, 7);
    f.cpu.execute(co(1));
    equal(f.cpu.read_control(EntryHi), 0x00400017);
    equal(f.cpu.read_control(EntryLo0), (0x2000 >> 6) | 0x16);
    f.cpu.state().gpr[1] = 0x00401000;
    f.memory.put(0x3000, 4, 0x87654321);
    f.cpu.execute(i(0x23, 1, 2, 0));
    equal(f.cpu.state().gpr[2], 0xffffffff87654321);
  }
  for (bool global : {false, true}) {
    Fixture f;
    map(f, 1, 0x00400017, 0x2000, global ? 0x17 : 0x16);
    f.cpu.write_control(EntryHi, 0x18);
    f.memory.put(0x2000, 4, 9);
    f.cpu.state().gpr[1] = 0x00400000;
    f.cpu.execute(i(0x23, 1, 2, 0));
    equal(f.cpu.state().gpr[2], global ? 9 : 0);
    equal(f.exception(), global ? 0 : 2);
    equal(f.cpu.state().pc, global ? 0xffffffffa0001008 : 0xffffffff80000000);
  }
  for (std::uint32_t mask : {0x6000u, 0x1e000u, 0x7e000u, 0x1fe000u, 0x7fe000u, 0x1ffe000u}) {
    Fixture f;
    map(f, 4, 0x02000000, 0x2000, 0x16, mask);
    const auto page = ((mask | 0x1fff) + 1) / 2;
    f.cpu.state().gpr[1] = 0x02000000 + page;
    f.memory.put(0x2000 + page, 4, page);
    f.cpu.execute(i(0x23, 1, 2, 0));
    equal(f.cpu.state().gpr[2], page);
  }
  {
    Fixture f;
    f.cpu.write_control(Status, 0x10);
    f.cpu.state().gpr[1] = 0xffffffffa0002000;
    f.cpu.execute(i(0x23, 1, 2, 0));
    equal(f.exception(), 4);
    equal(f.memory.transfers.size(), 0);
  }
  {
    Fixture f;
    f.cpu.write_control(Status, 0x10000080);
    f.cpu.state().gpr[1] = 0x9000000000002000;
    f.memory.put(0x2000, 8, 0x0123456789abcdef);
    f.cpu.execute(i(0x37, 1, 2, 0));
    equal(f.cpu.state().gpr[2], 0x0123456789abcdef);
    f.cpu.state().gpr[1] = 0x9000000100002000;
    f.cpu.execute(i(0x37, 1, 2, 0));
    equal(f.exception(), 4);
  }
  {
    Fixture f;
    f.cpu.write_control(Status, 0x80);
    f.cpu.state().gpr[1] = 0x0000001234000000;
    f.cpu.execute(i(0x23, 1, 2, 0));
    equal(f.exception(), 2);
    equal(f.cpu.state().pc, 0xffffffff80000080);
    equal(f.cpu.read_control(BadVAddr), 0x1234000000);
  }
  {
    Fixture f;
    f.cpu.write_control(Status, 0x10);
    f.cpu.execute(c(0, 1, Status));
    equal(f.exception(), 11);
    equal((f.cpu.read_control(Cause) >> 28) & 3, 0);
  }
  {
    Fixture f;
    f.cpu.write_control(EntryLo0, 0x3fffffff);
    f.cpu.write_control(EntryLo1, 0x3ffffffe);
    f.cpu.write_control(EntryHi, 0x12345042);
    f.cpu.write_control(PageMask, 0x2000);
    f.cpu.write_control(Index, 9);
    f.cpu.execute(co(2));
    f.cpu.execute(co(1));
    equal(f.cpu.read_control(EntryLo0), 0x03fffffe);
    equal(f.cpu.read_control(EntryLo1), 0x03fffffe);
    equal(f.cpu.read_control(PageMask), 0);
    f.cpu.write_control(32 + Epc, 0x12345678);
    equal(f.cpu.read_control(Epc), 0x12345678);
  }
  for (unsigned wired : {0u, 15u, 31u, 32u, 63u}) {
    Fixture f;
    f.cpu.write_control(Wired, wired);
    for (unsigned iteration = 0; iteration < 100; ++iteration) {
      const auto index = f.cpu.read_control(Random);
      equal(wired < 32 ? index >= wired && index < 32 : index < 64, true);
    }
  }
  {
    Fixture f;
    f.cpu.request_nmi();
    f.cpu.step();
    equal(f.cpu.state().pc, 0xffffffffbfc00000);
    equal(f.cpu.read_control(ErrorEpc), 0xffffffffa0001000);
    f.cpu.execute(co(0x18));
    equal(f.cpu.state().pc, 0xffffffffbfc00000);
    equal(f.cpu.read_control(ErrorEpc), 0xffffffffbfc00000);
    equal(f.cpu.read_control(Status) & 4, 4);
    f.cpu.power();
    f.cpu.write_control(ErrorEpc, 0xffffffffa0001000);
    f.cpu.execute(co(0x18));
    equal(f.cpu.state().pc, 0xffffffffa0001000);
    equal(f.cpu.read_control(Status) & 4, 0);
  }
}

} // namespace test
