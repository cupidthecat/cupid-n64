#include "../native_memory_fixture.hpp"

namespace test {
using namespace cupid::n64;
namespace {

enum Mapping { Cached, Direct, Mapped, Invalid, Misaligned };

struct AddressCase {
  std::uint64_t address;
  std::uint32_t physical;
  std::array<Mapping, 3> narrow, wide;
};

constexpr std::array<AddressCase, 28> addresses = {{
    {0xffffffff7ffffffc, 0, {Misaligned, Misaligned, Misaligned}, {Invalid, Invalid, Invalid}},
    {0xffffffff80000000, 0, {Cached, Cached, Cached}, {Cached, Cached, Cached}},
    {0xffffffff80000003, 3, {Cached, Cached, Cached}, {Cached, Cached, Cached}},
    {0xffffffff80001000, 0x1000, {Cached, Cached, Cached}, {Cached, Cached, Cached}},
    {0xffffffff80fffffc, 0xfffffc, {Cached, Cached, Cached}, {Cached, Cached, Cached}},
    {0xffffffff81000000, 0, {Cached, Cached, Cached}, {Cached, Cached, Cached}},
    {0xffffffff81000004, 4, {Cached, Cached, Cached}, {Cached, Cached, Cached}},
    {0xffffffff83effffc, 0x2effffc, {Cached, Cached, Cached}, {Cached, Cached, Cached}},
    {0xffffffff83efffff, 0x2efffff, {Cached, Cached, Cached}, {Cached, Cached, Cached}},
    {0xffffffff83f00000, 0x3f00000, {Cached, Invalid, Invalid}, {Cached, Invalid, Invalid}},
    {0xffffffff83ffffff, 0x3ffffff, {Cached, Invalid, Invalid}, {Cached, Invalid, Invalid}},
    {0xffffffff84000000, 0x4000000, {Cached, Invalid, Invalid}, {Cached, Invalid, Invalid}},
    {0xffffffff9ffffffc, 0x1ffffffc, {Cached, Invalid, Invalid}, {Cached, Invalid, Invalid}},
    {0xffffffffa0000000, 0, {Direct, Invalid, Invalid}, {Direct, Invalid, Invalid}},
    {0xffffffffa4000000, 0x4000000, {Direct, Invalid, Invalid}, {Direct, Invalid, Invalid}},
    {0xffffffffbffffffc, 0x1ffffffc, {Direct, Invalid, Invalid}, {Direct, Invalid, Invalid}},
    {0xffffffffc0000000, 0, {Mapped, Mapped, Invalid}, {Mapped, Mapped, Invalid}},
    {0xffffffffdffffffc, 0, {Mapped, Mapped, Invalid}, {Mapped, Mapped, Invalid}},
    {0xffffffffe0000000, 0, {Mapped, Invalid, Invalid}, {Mapped, Invalid, Invalid}},
    {0x80000000, 0, {Misaligned, Misaligned, Misaligned}, {Mapped, Mapped, Mapped}},
    {0xff80000000, 0, {Misaligned, Misaligned, Misaligned}, {Mapped, Mapped, Mapped}},
    {0x10000000000, 0, {Misaligned, Misaligned, Misaligned}, {Invalid, Invalid, Invalid}},
    {0x8000000000000000, 0, {Misaligned, Misaligned, Misaligned}, {Cached, Invalid, Invalid}},
    {0x8000000100000000, 0, {Misaligned, Misaligned, Misaligned}, {Invalid, Invalid, Invalid}},
    {0x8800000000000000, 0, {Misaligned, Misaligned, Misaligned}, {Cached, Invalid, Invalid}},
    {0x9000000000000000, 0, {Misaligned, Misaligned, Misaligned}, {Direct, Invalid, Invalid}},
    {0x9800000000000000, 0, {Misaligned, Misaligned, Misaligned}, {Cached, Invalid, Invalid}},
    {0xb8000000fffffffc,
     0xfffffffc,
     {Misaligned, Misaligned, Misaligned},
     {Cached, Invalid, Invalid}},
}};

struct AddressBus : Bus {
  bool cached = false;
  std::uint32_t physical = 0, lane_mask = 0;
  BusRead read(std::uint32_t address, unsigned) override {
    physical = address;
    return {0, 0, true};
  }
  BusWrite write(std::uint32_t address, unsigned, std::uint64_t) override {
    physical = address;
    return {0, true};
  }
  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> words) override {
    cached = true;
    physical = address;
    lane_mask = static_cast<std::uint32_t>(words.size() * 4 - 1);
    std::fill(words.begin(), words.end(), 0);
    return {0, true};
  }
};

} // namespace

void ram_address_tests() {
  for (unsigned privilege = 0; privilege < 4; ++privilege)
    for (bool extended : {false, true})
      for (unsigned level : {0u, 2u, 4u})
        for (bool reverse : {false, true})
          for (bool delay : {false, true})
            for (unsigned vector : {0u, 0x00400000u})
              for (const auto &test : addresses)
                for (unsigned access = 0; access < 3; ++access)
                  for (unsigned bytes : {1u, 2u, 4u}) {
                    if (access == 2 && bytes != 4)
                      continue;
                    AddressBus bus;
                    Cpu cpu{bus};
                    const auto status = 0x34000000u | (privilege << 3) | (extended ? 0xe0u : 0) |
                                        level | (reverse ? 0x02000000u : 0) | vector;
                    cpu.write_control(Status, status);
                    const auto pc = access == 2 ? test.address : 0xffffffffa0006000ull;
                    cpu.set_pc(pc);
                    if (delay) {
                      cpu.set_pc(pc - 4);
                      cpu.execute(i(4, 0, 0, 0));
                      equal(cpu.in_delay_slot(), true);
                    }
                    cpu.state().gpr[1] = test.address;
                    if (access == 2)
                      cpu.step();
                    else
                      cpu.execute(i(access == 1  ? bytes == 1   ? 40
                                                   : bytes == 2 ? 41
                                                                : 43
                                    : bytes == 1 ? 32
                                    : bytes == 2 ? 33
                                                 : 48,
                                    1, 8, 0));
                    const auto current = level ? 0u : std::min(privilege, 2u);
                    const auto mapping = test.address & (bytes - 1)
                                             ? Misaligned
                                             : (extended ? test.wide : test.narrow)[current];
                    const bool valid = mapping == Cached || mapping == Direct;
                    equal((cpu.read_control(Cause) & 0x7c) == 0, valid);
                    equal(bus.cached, valid && mapping == Cached);
                    if (valid) {
                      equal(bus.physical | (bus.cached ? test.address & bus.lane_mask : 0),
                            test.physical);
                      equal(cpu.read_control(LlAddr),
                            access == 0 && bytes == 4 ? test.physical >> 4 : 0);
                      equal(cpu.read_control(Cause), 0);
                      equal(cpu.read_control(BadVAddr), 0);
                      equal(cpu.read_control(Epc), 0);
                      equal(cpu.read_control(Status), status);
                      equal(cpu.read_control(Context), 0);
                      equal(cpu.read_control(XContext), 0);
                      equal(cpu.read_control(EntryHi), 0);
                    } else {
                      const auto cause = mapping == Mapped ? access == 1 ? 12u : 8u
                                         : access == 1     ? 20u
                                                           : 16u;
                      equal(cpu.read_control(Cause),
                            cause | (delay && level != 2 ? 0x80000000u : 0));
                      equal(cpu.read_control(BadVAddr), test.address);
                      equal(cpu.read_control(Epc), level == 2 ? 0 : pc - (delay ? 4 : 0));
                      const auto base = vector ? 0xffffffffbfc00200ull : 0xffffffff80000000ull;
                      const auto offset =
                          mapping == Mapped && level != 2 ? extended ? 0x80 : 0 : 0x180;
                      equal(cpu.state().pc, base + offset);
                      equal(cpu.in_delay_slot(), false);
                      equal(cpu.read_control(Status), status | 2);
                      equal(cpu.read_control(Context), (test.address >> 9) & 0x7ffff0);
                      equal(cpu.read_control(XContext), ((test.address >> 9) & 0x7ffffff0) |
                                                            ((test.address >> 31) & 0x180000000));
                      equal(cpu.read_control(EntryHi), test.address & 0xc00000ffffffe000);
                      equal(cpu.read_control(LlAddr), 0);
                      if (access == 2)
                        for (bool interpreted : {false, true}) {
                          AddressBus fault_bus;
                          Cpu fault{fault_bus};
                          fault.write_control(Status, status);
                          fault.set_pc(pc);
                          if (delay) {
                            fault.set_pc(pc - 4);
                            fault.execute(i(4, 0, 0, 0));
                          }
                          const auto start = fault.state().clocks;
                          equal(interpreted ? fault.run_interpreted_block(start + 10000)
                                            : fault.run_block(start + 10000),
                                true);
                          equal(fault.state().clocks - start, mapping == Misaligned ? 2 : 0);
                          equal(fault.state().pc, base + offset);
                          equal(fault.in_delay_slot(), false);
                          for (unsigned index :
                               {Cause, BadVAddr, Epc, Status, Context, XContext, EntryHi})
                            equal(fault.read_control(index), cpu.read_control(index));
                        }
                    }
                  }
}

void native_address_tests() {
  for (bool interpreted : {false, true})
    for (unsigned privilege = 0; privilege < 4; ++privilege)
      for (bool extended : {false, true})
        for (unsigned level : {0u, 2u, 4u})
          for (unsigned location = 0; location < 3; ++location)
            for (unsigned before = 0; before < 8; ++before) {
              native_memory::CachedFixture f;
              f.cpu.write_control(Status, 0x34000000);
              f.cpu.write_control(Index, 0);
              f.cpu.write_control(PageMask, 0);
              f.cpu.write_control(EntryHi, 0x00400000);
              f.cpu.write_control(EntryLo0, 0x5f);
              f.cpu.write_control(EntryLo1, 0x9f);
              f.cpu.execute(co(2));
              f.cpu.write_control(Status,
                                  0x34000000u | (privilege << 3) | (extended ? 0xe0u : 0) | level);
              f.cpu.write_control(Count, 0);
              f.cpu.write_control(Compare, 0xffffffff);
              const auto address = location == 0   ? 0xffffffff80001000ull
                                   : location == 1 ? 0xffffffff81001000ull
                                                   : 0x00400000ull;
              const auto target = location == 2 ? 0x00403000ull : 0xffffffff80003000ull;
              f.cpu.state().gpr[31] = target;
              f.cpu.set_pc(address);
              for (unsigned word = 0; word < before; ++word)
                f.code(word * 4, i(9, 6, 6, 1));
              f.code(before * 4, r(8, 31, 0, 0));
              f.code((before + 1) * 4, 0);
              const auto start = f.cpu.state().clocks;
              equal(interpreted ? f.cpu.run_interpreted_block(start + 10000)
                                : f.cpu.run_block(start + 10000),
                    true);
              const auto clocks = (before == 7 ? 192 : 96) + (before + 2) * 2;
              equal(f.cpu.state().clocks - start, clocks);
              equal(f.cpu.read_control(Count), clocks / 4);
              equal(f.cpu.state().gpr[6], before);
              equal(f.cpu.state().pc, target);
              equal(f.cpu.read_control(Cause), 0);
              equal(f.cpu.read_control(Epc), 0);
            }
}

} // namespace test
