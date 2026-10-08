#include "../../support/test.hpp"
#include <algorithm>

namespace test {
using namespace cupid::n64;
namespace {

struct TlbMemory : Memory {
  std::array<std::uint32_t, 8192> words{};
  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const override {
    return address < sizeof(words) ? std::span(words).subspan(address / 4)
                                   : std::span<const std::uint32_t>();
  }
  BusRead read(std::uint32_t address, unsigned bytes) override {
    if (address >= sizeof(words))
      return Memory::read(address, bytes);
    transfers.push_back({address, bytes, false, words[address / 4]});
    return {words[address / 4], clocks, success};
  }
  void put(std::uint32_t address, unsigned bytes, std::uint64_t value) {
    Memory::put(address, bytes, value);
    if (bytes == 4 && address < sizeof(words))
      words[address / 4] = static_cast<std::uint32_t>(value);
  }
};

struct TlbFixture {
  TlbMemory memory;
  Cpu cpu{memory};
  unsigned execution;
  unsigned code_offset = 0;
  explicit TlbFixture(unsigned execution) : execution(execution) {
    cpu.write_control(Status, 0x30000000);
    cpu.set_pc(0xffffffffa0001000ull);
  }

  void map(unsigned index, std::uint64_t address, std::uint32_t physical, unsigned flags0 = 0x16,
           unsigned flags1 = 0x16, std::uint32_t mask = 0, bool random = false) {
    cpu.write_control(Index, index);
    cpu.write_control(EntryHi, address);
    cpu.write_control(EntryLo0, (physical >> 6) | flags0);
    cpu.write_control(EntryLo1, ((physical + ((mask | 0x1fff) + 1) / 2) >> 6) | flags1);
    cpu.write_control(PageMask, mask);
    if (random)
      cpu.write_control(Wired, 31);
    cpu.execute(co(random ? 6 : 2));
    for (unsigned offset = 0; offset <= (mask | 0x1fff); offset += 0x1000)
      memory.put(physical + offset, 4, physical + offset);
  }

  void asid(unsigned id) {
    cpu.write_control(EntryHi, id);
  }

  void expect(std::uint64_t address, std::uint32_t physical, unsigned exception = 0,
              bool store = false, bool delay = false, bool miss = false) {
    cpu.write_control(Status, cpu.read_control(Status) & ~2ull);
    const auto pc = execution ? 0xffffffff80001000ull + code_offset : 0xffffffffa0001000ull;
    cpu.set_pc(pc);
    cpu.state().gpr[1] = address;
    cpu.state().gpr[4] = 0x7fffffff;
    cpu.state().gpr[5] = cpu.read_control(Status);
    memory.transfers.clear();
    const auto instruction = i(store ? 0x2b : 0x23, 1, 4, 0);
    const auto end = pc + (delay ? 16 : execution ? 8 : 4);
    if (!execution) {
      if (delay)
        cpu.execute(i(4, 0, 0, 3));
      cpu.execute(instruction);
    } else {
      auto code = [&](unsigned offset, std::uint32_t value) {
        memory.words[(0x1000 + code_offset + offset) / 4] = value;
      };
      code(0, delay ? i(4, 0, 0, 3) : instruction);
      code(4, delay ? instruction : c(4, 5, Status));
      for (unsigned offset = 8; offset <= 16; offset += 4)
        code(offset, c(4, 5, Status));
      for (unsigned batches = 0; batches < 8 && cpu.state().pc != end; ++batches) {
        const auto target = cpu.state().clocks + 1024;
        const bool ran = execution == 1 ? cpu.run_interpreted_block(target) : cpu.run_block(target);
        equal(ran, true);
        if (!ran)
          cpu.step();
        if (cpu.read_control(Status) & 2)
          break;
      }
      code_offset += 32;
    }
    equal(cpu.in_delay_slot(), false);
    if (exception) {
      equal((cpu.read_control(Cause) >> 2) & 31, exception);
      const auto vector = miss ? (cpu.read_control(Status) & 0x80 ? 0x80u : 0u) : 0x180u;
      equal(cpu.state().pc, 0xffffffff80000000ull + vector);
      equal(cpu.read_control(BadVAddr), address);
      equal(cpu.read_control(Epc), pc);
      equal(cpu.read_control(Cause) >> 31, delay);
      equal(cpu.state().gpr[4], 0x7fffffff);
      equal(std::count_if(memory.transfers.begin(), memory.transfers.end(),
                          [](const auto &transfer) { return transfer.address >= 0x100000; }),
            0);
      return;
    }
    equal(cpu.state().pc, end);
    equal(cpu.read_control(Status) & 2, 0);
    if (!store) {
      equal(cpu.state().gpr[4], physical);
      return;
    }
    // A cached store reaches the bus when its data line is written back.
    if (std::none_of(memory.transfers.begin(), memory.transfers.end(),
                     [](const auto &transfer) { return transfer.store; }))
      cpu.execute(i(0x2f, 1, 0x19, 0));
    const auto transfer =
        std::find_if(memory.transfers.begin(), memory.transfers.end(), [](const auto &transfer) {
          return transfer.store && transfer.value == 0x7fffffff;
        });
    equal(transfer != memory.transfers.end(), true);
    if (transfer != memory.transfers.end()) {
      equal(transfer->address, physical);
      equal(transfer->bytes, 4);
    }
  }
};

void overlap_tests(unsigned execution) {
  // These overlapping mappings pin lookup history rather than a hardware priority rule.
  for (bool cached : {false, true})
    for (unsigned flags : {0u, 2u, 6u})
      for (bool warm : {false, true})
        for (bool store : {false, true})
          for (bool delay : {false, true}) {
            TlbFixture f(execution);
            const unsigned cache = cached ? 0x18 : 0x10;
            f.map(0, 0x40001, 0x100000, flags | cache, flags | cache);
            f.map(1, 0x40001, 0x200000, 6 | cache, 6 | cache, 0x6000);
            f.asid(1);
            if (warm)
              f.expect(0x42000, 0x202000);
            const unsigned exception = warm                    ? 0
                                       : !(flags & 2)          ? (store ? 3 : 2)
                                       : store && !(flags & 4) ? 1
                                                               : 0;
            f.expect(0x40000, warm ? 0x200000 : 0x100000, exception, store, delay);
          }
}

void frequency_tests(unsigned execution) {
  struct History {
    std::vector<unsigned> accesses;
    unsigned selected;
  };
  const std::array<History, 8> histories{{
      {{0, 1, 2, 3, 4}, 4},
      {{0, 0, 1, 1, 2, 2, 3, 3, 4}, 4},
      {{0, 0, 1, 1, 2, 2, 3, 3, 4, 0}, 0},
      {{0, 0, 1, 1, 2, 2, 3, 3, 4, 1}, 4},
      {{3, 3, 2, 2, 1, 1, 0, 0, 4}, 4},
      {{4, 4, 0, 0, 1, 1, 2, 2, 3}, 3},
      {{2, 2, 2, 2, 3, 3, 1, 1, 0, 0, 4}, 2},
      {{4, 4, 4, 4, 0, 0, 1, 1, 2, 2, 3, 3, 0}, 4},
  }};
  for (const auto &history : histories) {
    TlbFixture f(execution);
    for (unsigned index = 0; index < 5; ++index)
      f.map(index, 0x40001 + index * 0x10000, 0x100000 + index * 0x10000);
    f.asid(1);
    for (const auto index : history.accesses)
      f.expect(0x40000 + index * 0x10000, 0x100000 + index * 0x10000);
    for (unsigned index = 0; index < 5; ++index)
      f.map(index, 0x40001, 0x100000 + index * 0x10000);
    f.asid(1);
    f.expect(0x40000, 0x100000 + history.selected * 0x10000);
    f.expect(0x41000, 0x101000 + history.selected * 0x10000, 0, true);
  }
  for (bool store : {false, true}) {
    TlbFixture f(execution);
    f.map(0, 0x40001, 0x100000, store ? 0x12 : 0x10);
    f.asid(1);
    f.expect(0x40000, 0, store ? 1 : 2, store);
    f.expect(0x40000, 0, store ? 1 : 2, store);
    for (unsigned index = 1; index < 5; ++index) {
      f.map(index, 0x40001 + index * 0x10000, 0x100000 + index * 0x10000);
      f.asid(1);
      f.expect(0x40000 + index * 0x10000, 0x100000 + index * 0x10000);
    }
    for (unsigned index = 0; index < 5; ++index)
      f.map(index, 0x40001, 0x100000 + index * 0x10000);
    f.asid(1);
    f.expect(0x40000, 0x100000);
  }
}

void tag_tests(unsigned execution) {
  for (unsigned globals = 0; globals < 4; ++globals) {
    TlbFixture f(execution);
    f.map(0, 0x40002, 0x100000);
    f.map(1, 0x40001, 0x200000, 0x16 | (globals & 1), 0x16 | (globals >> 1), 0x6000);
    f.asid(1);
    f.expect(0x42000, 0x202000);
    f.asid(2);
    f.expect(0x40000, globals == 3 ? 0x200000 : 0x100000);
  }
  for (unsigned target : {0u, 1u, 3u})
    for (unsigned warmed : {0u, 1u, 3u}) {
      TlbFixture f(execution);
      f.map(0, (std::uint64_t(target) << 62) | 0x40001, 0x100000);
      f.map(1, (std::uint64_t(warmed) << 62) | 0x40001, 0x200000, 0x16, 0x16, 0x6000);
      f.cpu.write_control(Status, 0x30000080);
      f.asid(1);
      f.expect((std::uint64_t(warmed) << 62) | 0x42000, 0x202000);
      f.expect((std::uint64_t(target) << 62) | 0x40000, target == warmed ? 0x200000 : 0x100000);
    }
}

void rewrite_tests(unsigned execution) {
  TlbFixture f(execution);
  f.map(0, 0x40001, 0x100000);
  f.map(1, 0x40001, 0x200000, 0x16, 0x16, 0x6000);
  f.asid(1);
  f.expect(0x42000, 0x202000);
  f.map(1, 0x40001, 0x300000, 0x16, 0x16, 0x6000);
  f.asid(1);
  f.expect(0x40000, 0x300000);
  f.map(1, 0x40002, 0x300000, 0x16, 0x16, 0x6000);
  f.asid(1);
  f.expect(0x40000, 0x100000);
  f.map(1, 0x40001, 0x300000, 0x10, 0x16, 0x6000);
  f.asid(1);
  f.expect(0x40000, 0, 2);
  f.map(1, 0x40001, 0x300000, 0x12, 0x16, 0x6000);
  f.asid(1);
  f.expect(0x40000, 0, 1, true);
  f.expect(0x44000, 0x304000, 0, true);
  f.map(1, 0x40001, 0x300000);
  f.asid(1);
  f.expect(0x42000, 0, 2, false, true, true);
  f.expect(0x41000, 0x301000);

  TlbFixture reset(execution);
  reset.map(0, 0x40001, 0x100000);
  reset.map(1, 0x40001, 0x200000, 0x16, 0x16, 0x6000);
  reset.asid(1);
  reset.expect(0x42000, 0x202000);
  reset.cpu.power();
  reset.cpu.write_control(Status, 0x30000000);
  reset.asid(1);
  reset.expect(0x40000, 0, 2, false, false, true);
  reset.map(0, 0x40001, 0x100000);
  reset.map(1, 0x40001, 0x200000, 0x16, 0x16, 0x6000);
  reset.asid(1);
  reset.expect(0x40000, 0x200000);

  TlbFixture random(execution);
  random.map(0, 0x40001, 0x100000);
  random.map(31, 0x40001, 0x200000, 0x16, 0x16, 0x6000);
  random.asid(1);
  random.expect(0x42000, 0x202000);
  random.map(31, 0x40001, 0x300000, 0x16, 0x16, 0x6000, true);
  random.asid(1);
  random.expect(0x40000, 0x300000);
}

void mapped_fetch_tests(unsigned execution) {
  TlbFixture f(execution);
  f.map(0, 0x40001, 0x2000, 0x1e, 0x1e);
  f.map(1, 0x40001, 0x4000, 0x1e, 0x1e, 0x6000);
  f.asid(1);
  f.expect(0x42000, 0x6000);
  auto code = [&](unsigned physical, unsigned value) {
    f.memory.put(physical, 4, i(9, 0, 2, static_cast<std::uint16_t>(value)));
    f.memory.put(physical + 4, 4, c(4, 5, Status));
  };
  code(0x2000, 11);
  code(0x4000, 22);
  auto run = [&](unsigned value) {
    f.cpu.state().gpr[2] = 0;
    f.cpu.state().gpr[5] = 0x30000000;
    f.cpu.set_pc(0x40000);
    if (!execution) {
      f.cpu.step();
      f.cpu.step();
    } else {
      const auto target = f.cpu.state().clocks + 1024;
      equal(execution == 1 ? f.cpu.run_interpreted_block(target) : f.cpu.run_block(target), true);
    }
    equal(f.cpu.state().gpr[2], value);
    equal(f.cpu.state().pc, 0x40008);
    equal(f.cpu.read_control(Status), 0x30000000);
  };
  run(22);
  f.map(1, 0x40001, 0x5000, 0x1e, 0x1e);
  f.asid(1);
  code(0x5000, 33);
  run(33);
  f.map(1, 0x50001, 0x5000, 0x1e, 0x1e);
  f.asid(1);
  run(11);
}

} // namespace

void tlb_lookup_tests() {
  for (unsigned execution = 0; execution < 3; ++execution) {
    overlap_tests(execution);
    frequency_tests(execution);
    tag_tests(execution);
    rewrite_tests(execution);
    mapped_fetch_tests(execution);
  }
}

} // namespace test
