#include "../support/test.hpp"

namespace test {
using namespace cupid::n64;

namespace {
struct CodeMemory : Memory {
  std::array<std::uint32_t, 2048> words{};
  std::span<const std::uint32_t> instruction_data(std::uint32_t address) const override {
    return address < sizeof(words) ? std::span(words).subspan(address >> 2)
                                   : std::span<const std::uint32_t>();
  }
  BusWrite read_burst(std::uint32_t address, std::span<std::uint32_t> dest) override {
    if (success)
      for (unsigned n = 0; n < dest.size(); ++n)
        dest[n] = words[((address >> 2) + n) % words.size()];
    return {clocks, success};
  }
};
struct BlockFixture {
  CodeMemory memory;
  Cpu cpu{memory};
  std::uint64_t target = 0;
  BlockFixture() {
    cpu.write_control(Status, 0x30000000);
    cpu.set_pc(0xffffffff80001000);
  }
  void code(unsigned offset, std::uint32_t instruction) {
    memory.words[1024 + offset / 4] = instruction;
  }
  void run() {
    equal(cpu.run_block(target), true);
  }
};
} // namespace

void execution_tests() {
  for (auto immediate : {std::uint16_t(7), std::uint16_t(0xffff)}) {
    BlockFixture f;
    f.cpu.state().gpr[1] = 0x123456789abcdef0;
    f.code(0, i(9, 0, 1, immediate));
    f.code(4, i(11, 0, 0, 1));
    f.code(8, i(9, 0, 2, 3));
    f.code(12, 0x08000400);
    f.code(16, 0);
    f.run();
    equal(f.cpu.state().gpr[1], immediate == 7 ? 7 : ~0ull);
    equal(f.cpu.state().gpr[0], 0);
    equal(f.cpu.state().gpr[2], 3);
    f.cpu.power();
    f.cpu.write_control(Status, 0x30000000);
    f.cpu.set_pc(0xffffffff80001000);
    f.run();
    equal(f.cpu.state().gpr[1], immediate == 7 ? 7 : ~0ull);
    equal(f.cpu.state().clocks, 106);
  }
  {
    std::uint64_t random = 0x457238419ba43ull;
    const auto next = [&] {
      random ^= random << 13;
      random ^= random >> 7;
      random ^= random << 17;
      return random;
    };
    constexpr std::array functions{0u,  2u,  3u,  4u,  6u,  7u,  15u, 16u, 17u, 18u,
                                   19u, 20u, 22u, 23u, 33u, 35u, 36u, 37u, 38u, 39u,
                                   42u, 43u, 45u, 47u, 56u, 58u, 59u, 60u, 62u, 63u};
    constexpr std::array opcodes{9u, 10u, 11u, 12u, 13u, 14u, 15u, 25u};
    for (unsigned trial = 0; trial < 128; ++trial) {
      BlockFixture native;
      BlockFixture interpreted;
      const bool little = trial & 1;
      for (auto *fixture : {&native, &interpreted}) {
        fixture->cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
        fixture->target = 0;
      }
      for (unsigned reg = 0; reg < 32; ++reg) {
        const auto value = reg ? next() : 0;
        native.cpu.state().gpr[reg] = interpreted.cpu.state().gpr[reg] = value;
      }
      native.cpu.state().hi = interpreted.cpu.state().hi = next();
      native.cpu.state().lo = interpreted.cpu.state().lo = next();
      for (unsigned word = 0; word < 64; ++word) {
        const auto rs = static_cast<unsigned>(next() & 31);
        const auto rt = static_cast<unsigned>(next() & 31);
        const auto dest = static_cast<unsigned>(next() % 28);
        const auto instruction = word & 1 ? r(functions[next() % functions.size()], rs, rt, dest,
                                              static_cast<unsigned>(next() & 31))
                                          : i(opcodes[next() % opcodes.size()], rs, dest,
                                              static_cast<std::uint16_t>(next()));
        const auto offset = (word ^ unsigned(little)) * 4;
        native.code(offset, instruction);
        interpreted.code(offset, instruction);
      }
      for (auto *fixture : {&native, &interpreted}) {
        fixture->code((64 ^ unsigned(little)) * 4, 0x08000400);
        fixture->code((65 ^ unsigned(little)) * 4, 0);
      }
      for (unsigned run = 0; run < 3; ++run) {
        equal(native.cpu.run_block(native.target), true);
        equal(interpreted.cpu.run_interpreted_block(interpreted.target), true);
        for (unsigned reg = 0; reg < 32; ++reg)
          equal(native.cpu.state().gpr[reg], interpreted.cpu.state().gpr[reg]);
        equal(native.cpu.state().hi, interpreted.cpu.state().hi);
        equal(native.cpu.state().lo, interpreted.cpu.state().lo);
        equal(native.cpu.state().pc, interpreted.cpu.state().pc);
        equal(native.cpu.state().clocks, interpreted.cpu.state().clocks);
        equal(native.cpu.read_control(Count), interpreted.cpu.read_control(Count));
        equal(native.cpu.read_control(Cause), interpreted.cpu.read_control(Cause));
        equal(native.cpu.in_delay_slot(), interpreted.cpu.in_delay_slot());
      }
    }
  }
  {
    BlockFixture f;
    f.code(0, i(9, 0, 1, 42));
    f.memory.success = false;
    f.run();
    equal(f.cpu.state().clocks, 98);
    equal(f.cpu.state().gpr[1], 0);
    equal(f.cpu.read_control(Epc), 0xffffffff80001000);
    equal(f.cpu.state().pc, 0xffffffff80000180);
    equal((f.cpu.read_control(Cause) >> 2) & 31, static_cast<unsigned>(Exception::BusInstruction));
  }
  {
    BlockFixture f;
    f.code(0, i(9, 0, 1, 42));
    f.code(4, 0x08000400);
    f.code(8, i(9, 1, 2, 1));
    f.run();
    equal(f.cpu.state().gpr[1], 42);
    equal(f.cpu.state().gpr[2], 43);
    equal(f.cpu.state().pc, 0xffffffff80001000);
    equal(f.cpu.state().clocks, 102);
    f.run();
    equal(f.cpu.state().clocks, 108);
  }
  {
    BlockFixture f;
    f.code(0, 0x1000ffff);
    f.code(4, i(9, 2, 2, 1));
    f.run();
    equal(f.cpu.state().gpr[2], 1);
    equal(f.cpu.state().clocks, 226);
    equal(f.cpu.state().pc, 0xffffffff80001000);
    f.run();
    equal(f.cpu.state().gpr[2], 2);
    equal(f.cpu.state().clocks, 356);
    f.code(0, 0x08000400);
    equal(f.cpu.run_block(f.target), true);
    equal(f.cpu.state().gpr[2], 3);
    equal(f.cpu.state().clocks, 486);
    f.cpu.set_pc(0xffffffff80001000);
    f.cpu.state().gpr[3] = 0xffffffff80001000;
    f.cpu.execute(i(47, 3, 16, 0));
    f.cpu.set_pc(0xffffffff80001000);
    const auto start = f.cpu.state().clocks;
    f.run();
    equal(f.cpu.state().clocks - start, 226);
  }
  for (bool taken : {false, true}) {
    BlockFixture f;
    f.code(0, i(20, 1, 0, 3));
    f.code(4, i(9, 0, 2, 7));
    f.cpu.state().gpr[1] = taken ? 0 : 1;
    f.run();
    equal(f.cpu.state().gpr[2], taken ? 7 : 0);
    equal(f.cpu.state().pc, taken ? 0xffffffff80001010 : 0xffffffff80001008);
    equal(f.cpu.state().clocks, taken ? 100 : 98);
    equal(f.cpu.in_delay_slot(), false);
  }
  {
    BlockFixture f;
    f.code(0, i(5, 0, 0, 1));
    f.code(4, i(9, 0, 2, 7));
    f.code(8, i(9, 0, 3, 8));
    f.run();
    equal(f.cpu.state().pc, 0xffffffff80001008);
    equal(f.cpu.state().gpr[2], 7);
    equal(f.cpu.state().gpr[3], 0);
  }
  {
    BlockFixture f;
    f.code(0, i(9, 0, 1, 7));
    f.code(4, i(9, 0, 28, 9));
    f.code(8, i(9, 0, 2, 8));
    f.run();
    equal(f.cpu.state().gpr[28], 9);
    equal(f.cpu.state().gpr[2], 0);
    equal(f.cpu.state().pc, 0xffffffff80001008);
  }
  {
    BlockFixture f;
    f.code(0, i(9, 0, 1, 7));
    f.code(4, r(13, 0, 0, 0));
    f.code(8, i(9, 0, 2, 8));
    f.run();
    equal(f.cpu.state().gpr[1], 7);
    equal(f.cpu.state().gpr[2], 0);
    equal(f.cpu.read_control(Epc), 0xffffffff80001004);
    equal(f.cpu.state().pc, 0xffffffff80000180);
  }
  {
    BlockFixture f;
    f.code(0, i(4, 0, 0, 2));
    f.code(4, r(13, 0, 0, 0));
    f.run();
    equal(f.cpu.read_control(Epc), 0xffffffff80001000);
    equal(f.cpu.read_control(Cause) & 0x80000000, 0x80000000);
    equal(f.cpu.state().pc, 0xffffffff80000180);
  }
  {
    BlockFixture f;
    f.code(0, i(9, 0, 1, 7));
    f.cpu.set_pc(0xffffffffa0001000);
    equal(f.cpu.run_block(f.target), false);
    f.cpu.set_pc(0x0000000080001000);
    equal(f.cpu.run_block(f.target), true);
    equal(f.cpu.read_control(Cause), 16);
    equal(f.cpu.read_control(BadVAddr), 0x0000000080001000);
    equal(f.cpu.read_control(Epc), 0x0000000080001000);
    equal(f.cpu.state().pc, 0xffffffff80000180);
  }
  {
    BlockFixture f;
    f.code(0, i(9, 0, 1, 7));
    f.code(4, i(5, 0, 0, 1));
    f.code(8, i(9, 0, 2, 8));
    f.cpu.write_control(Compare, 1);
    f.cpu.write_control(Status, 0x30008001);
    unsigned requests = 0;
    f.cpu.connect_sync([&] {
      ++requests;
      f.target = 0;
    });
    f.run();
    equal(requests, 0);
    f.cpu.synchronize_timer();
    equal(requests > 0, true);
    equal(f.cpu.state().gpr[2], 8);
    equal(f.cpu.state().pc, 0xffffffff8000100c);
    f.run();
    equal(f.cpu.state().pc, 0xffffffff80000180);
    equal(f.cpu.read_control(Epc), 0xffffffff8000100c);
    equal(f.cpu.read_control(Cause) & 0xff00, 0x8000);
  }
  {
    BlockFixture f;
    f.cpu.write_control(Count, 100);
    f.cpu.write_control(Compare, 150);
    equal(f.cpu.synchronization_limit(), 100);
    f.cpu.advance_clocks(40);
    equal(f.cpu.synchronization_limit(), 80);
    f.cpu.write_control(Compare, 50);
    equal(f.cpu.synchronization_limit(), (1ull << 33) - 120);
  }
  {
    BlockFixture f;
    f.code(0, co(24));
    f.cpu.write_control(Epc, 0xffffffff80001000);
    f.cpu.write_control(Status, 0x30000002);
    f.run();
    equal(f.cpu.state().pc, 0xffffffff80001000);
    equal(f.cpu.state().clocks, 98);
  }
  {
    BlockFixture f;
    f.code(0, co(24));
    f.cpu.write_control(Epc, 0xffffffff80001004);
    f.cpu.write_control(Status, 0x30008003);
    f.cpu.set_interrupt(7, true);
    unsigned requests = 0;
    f.target = 4096;
    f.cpu.connect_sync([&] {
      ++requests;
      f.target = f.cpu.state().clocks;
    });
    f.run();
    equal(requests, 1);
    equal(f.target, 96);
    equal(f.cpu.state().clocks, 98);
    equal(f.cpu.state().pc, 0xffffffff80001004);
    f.run();
    equal(requests, 2);
    equal(f.cpu.state().pc, 0xffffffff80000180);
    equal(f.cpu.read_control(Epc), 0xffffffff80001004);
  }
}

} // namespace test
