#include "../native_memory_fixture.hpp"

namespace test {
using namespace cupid::n64;
namespace {

constexpr auto latch = 0x8123456789abcdefull;
constexpr auto saved_epc = 0x12345678ull;

void initialize(Cpu &cpu, unsigned status) {
  cpu.write_control(Status, 0x70000000);
  cpu.state().gpr[7] = latch;
  cpu.execute((0x12u << 26) | (5u << 21) | (7u << 16));
  cpu.write_control(Status, status);
  cpu.write_control(Epc, saved_epc);
  for (unsigned reg = 1; reg < 32; ++reg)
    cpu.state().gpr[reg] = 0xffffffffdead0003ull + reg * 0x1000;
}

void check_exception(Cpu &cpu, unsigned status, std::uint64_t start, bool delay,
                     const std::array<std::uint64_t, 32> &registers) {
  const auto cause = cpu.read_control(Cause);
  equal((cause >> 2) & 31, status & 0x40000000 ? 10 : 11);
  equal((cause >> 28) & 3, 2);
  equal((cause >> 31) & 1, delay && !(status & 2));
  equal(cpu.read_control(Epc), status & 2 ? saved_epc : start);
  equal(cpu.read_control(Status), status | 2);
  equal(cpu.read_control(BadVAddr), 0);
  equal(cpu.state().pc, status & 0x00400000 ? 0xffffffffbfc00380ull : 0xffffffff80000180ull);
  equal(cpu.in_delay_slot(), false);
  for (unsigned reg = 0; reg < registers.size(); ++reg)
    equal(cpu.state().gpr[reg], registers[reg]);
  cpu.write_control(Status, 0x70000000);
  cpu.set_pc(0xffffffffa0001000);
  cpu.execute((0x12u << 26) | (1u << 21) | (8u << 16));
  equal(cpu.state().gpr[8], latch);
}

void direct() {
  Fixture f;
  for (auto status : {0x30000000u, 0x70000000u, 0x70000008u, 0x70000010u, 0x70000030u, 0x70400000u,
                      0x70000002u}) {
    for (auto operation : {0x32u, 0x36u, 0x3au, 0x3eu}) {
      for (unsigned base = 0; base < 32; ++base) {
        for (auto target : {0u, 1u, 2u, 28u, 29u, 31u}) {
          for (auto offset : {0u, 1u, 0xffffu}) {
            for (bool delay : {false, true}) {
              f.cpu.power();
              initialize(f.cpu, status);
              f.cpu.set_pc(0xffffffffa0001000);
              if (delay)
                f.cpu.execute(i(4, 0, 0, 8));
              const auto registers = f.cpu.state().gpr;
              const auto clocks = f.cpu.state().clocks;
              f.cpu.execute(i(operation, base, target, static_cast<std::uint16_t>(offset)));
              equal(f.cpu.state().clocks, clocks + 2);
              equal(f.memory.transfers.size(), 0);
              check_exception(f.cpu, status, 0xffffffffa0001000, delay, registers);
            }
          }
        }
      }
    }
  }
}

void fetched() {
  for (auto status : {0x30000000u, 0x70000000u, 0x70400000u, 0x70000002u}) {
    for (auto operation : {0x32u, 0x36u, 0x3au, 0x3eu}) {
      for (unsigned base = 0; base < 32; ++base) {
        for (bool delay : {false, true}) {
          Fixture f;
          initialize(f.cpu, status);
          f.cpu.set_pc(0xffffffffa0001000);
          f.code(0, delay ? i(4, 0, 0, 8) : i(operation, base, 2, 1));
          f.code(4, i(operation, base, 2, 1));
          const auto registers = f.cpu.state().gpr;
          const auto clocks = f.cpu.state().clocks;
          f.cpu.step();
          if (delay)
            f.cpu.step();
          equal(f.cpu.state().clocks, clocks + (delay ? 4 : 2));
          equal(f.memory.transfers.size(), delay ? 2 : 1);
          check_exception(f.cpu, status, 0xffffffffa0001000, delay, registers);
        }
      }
    }
  }
}

void blocks() {
  for (bool interpreted : {false, true}) {
    for (bool little : {false, true}) {
      for (auto status : {0x30000000u, 0x70000000u, 0x70400000u, 0x70000002u}) {
        for (auto operation : {0x32u, 0x36u, 0x3au, 0x3eu}) {
          for (unsigned base = 0; base < 32; ++base) {
            for (auto target : {0u, 2u, 29u}) {
              for (bool delay : {false, true}) {
                native_memory::CachedFixture f;
                initialize(f.cpu, status);
                f.cpu.write_control(Config, little ? 0x70066460 : 0x7006e460);
                const auto offset = delay ? 4088u : 4092u;
                const auto start = 0xffffffff80001000ull + offset;
                f.cpu.set_pc(start);
                f.code(offset, delay ? i(4, 0, 0, 8) : i(operation, base, target, 1), little);
                f.code(4092, i(operation, base, target, 1), little);
                const auto registers = f.cpu.state().gpr;
                const auto clocks = f.cpu.state().clocks;
                equal(interpreted ? f.cpu.run_interpreted_block(clocks) : f.cpu.run_block(clocks),
                      true);
                equal(f.cpu.state().clocks, clocks + 96 + (delay ? 4 : 2));
                equal(f.memory.transfers.size(), 1);
                if (!f.memory.transfers.empty()) {
                  equal(f.memory.transfers.front().store, false);
                  equal(f.memory.transfers.front().address, 0x1fe0);
                  equal(f.memory.transfers.front().bytes, 32);
                }
                check_exception(f.cpu, status, start, delay, registers);
              }
            }
          }
        }
      }
    }
  }
}

} // namespace

void cop2_memory_tests() {
  direct();
  fetched();
  blocks();
}

} // namespace test
