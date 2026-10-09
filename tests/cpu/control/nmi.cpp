#include "../../support/test.hpp"

namespace test {
using namespace cupid::n64;
namespace {
constexpr std::uint64_t reset_vector = 0xffffffffbfc00000ull;

void poll(Cpu &cpu, unsigned route) {
  if (route == 1 && cpu.run_block(cpu.state().clocks + 1))
    return;
  if (route == 2 && cpu.run_interpreted_block(cpu.state().clocks + 1))
    return;
  cpu.step();
}

void repeated_entry(unsigned route, unsigned status) {
  Fixture f;
  f.cpu.write_control(Status, status);
  f.cpu.write_control(Epc, 0x12345678);
  f.cpu.set_pc(0xffffffff80001000ull);
  f.cpu.state().gpr[7] = 0x89abcdef01234567ull;
  f.cpu.request_nmi();
  const auto clocks = f.cpu.state().clocks;
  auto saved_pc = f.cpu.state().pc;
  for (unsigned entry = 0; entry < 3; ++entry) {
    poll(f.cpu, route);
    equal(f.cpu.state().pc, reset_vector);
    equal(f.cpu.state().clocks, clocks + (entry + 1) * 2);
    equal(f.cpu.read_control(ErrorEpc), saved_pc);
    equal(f.cpu.read_control(Epc), 0x12345678);
    equal(f.cpu.read_control(Status), (status | 0x00400004u) & ~0x00300000u);
    equal(f.cpu.state().gpr[7], 0x89abcdef01234567ull);
    equal(f.cpu.in_delay_slot(), false);
    saved_pc = reset_vector;
  }
  const auto before_return = f.cpu.state().clocks;
  f.cpu.execute(co(24));
  equal(f.cpu.state().pc, reset_vector);
  equal(f.cpu.state().clocks, before_return + 2);
  equal(f.cpu.read_control(Status) & 4, 4);
  poll(f.cpu, route);
  equal(f.cpu.state().pc, reset_vector);
  equal(f.cpu.read_control(Status) & 4, 4);
}

void interrupt_priority(unsigned route) {
  Fixture f;
  f.cpu.write_control(Status, 0x10000401);
  f.cpu.set_pc(0xffffffff80001000ull);
  f.cpu.write_control(ErrorEpc, 0x12345678);
  f.cpu.set_interrupt(2, true);
  f.cpu.request_nmi();
  poll(f.cpu, route);
  equal(f.cpu.state().pc, 0xffffffff80000180ull);
  equal(f.cpu.read_control(Epc), 0xffffffff80001000ull);
  equal(f.cpu.read_control(ErrorEpc), 0x12345678);
  equal(f.cpu.state().clocks, 2);
  poll(f.cpu, route);
  equal(f.cpu.state().pc, reset_vector);
  equal(f.cpu.read_control(ErrorEpc), 0xffffffff80000180ull);
  equal(f.cpu.state().clocks, 4);
  poll(f.cpu, route);
  equal(f.cpu.state().pc, reset_vector);
  equal(f.cpu.read_control(ErrorEpc), reset_vector);
  equal(f.cpu.state().clocks, 6);
}

void power_clears_request(unsigned route) {
  Fixture f;
  f.cpu.request_nmi();
  poll(f.cpu, route);
  f.cpu.power();
  f.memory.put(0x1000, 4, i(9, 0, 7, 1));
  f.cpu.set_pc(0xffffffffa0001000ull);
  poll(f.cpu, route);
  equal(f.cpu.state().pc, 0xffffffffa0001004ull);
  equal(f.cpu.state().gpr[7], 1);
  equal(f.cpu.state().clocks, 2);
}
} // namespace

void nmi_tests() {
  for (unsigned route = 0; route < 3; ++route) {
    for (unsigned status : {0x10000000u, 0x10300000u, 0x10000002u, 0x10000004u})
      repeated_entry(route, status);
    interrupt_priority(route);
    power_clears_request(route);
  }
}
} // namespace test
