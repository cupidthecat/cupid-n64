#include "../fixture.hpp"
#include "core/system/console.hpp"

namespace test {
using namespace cupid::n64;

void timer_dispatch_tests() {
  auto machine = std::make_unique<Console>();
  for (unsigned initial : {0u, 10u, 0xffffffffu}) {
    machine->power();
    machine->write(0x04700008, 4, 0);
    machine->write(0x0470000c, 4, 0x14);
    machine->write(0x04300000, 4, 0x10f);
    machine->write(0x03f80008, 4, 0x00080008);
    for (unsigned chip = 0; chip < 4; ++chip) {
      machine->write(0x03f0000c, 4, 0x02000000);
      machine->write(0x03f00004, 4, (chip + 4) * 2 << 26);
    }
    for (unsigned chip = 0; chip < 4; ++chip)
      machine->write(0x03f00004 + (chip + 4) * 0x800, 4, chip * 2 << 26);
    auto &cpu = machine->cpu();
    auto &video = machine->video();
    cpu.write_control(Status, 0x30000000);
    cpu.set_pc(0xffffffff80001000);
    machine->write(0x1000, 4, 0x1000ffff);
    machine->write(0x1004, 4, 0);
    cpu.write_control(Count, initial);
    cpu.write_control(Compare, initial == 10 ? 9 : 0);
    video.write_word(0, 2);
    video.write_word(24, 525);
    video.write_word(28, 256);
    video.write_word(32, 256);
    video.write_word(40, 1 << 17);
    unsigned callbacks = 0;
    video.connect_frame([&](bool) {
      ++callbacks;
      equal(cpu.state().clocks, 226);
      equal(cpu.read_control(Count), initial);
    });
    equal(machine->run_interval(), 226);
    equal(callbacks, 1);
    equal(cpu.state().pc, 0xffffffff80001000);
    equal(cpu.read_control(Count), std::uint32_t(initial + 56));
    equal(cpu.read_control(Cause) & 0x8000, initial == 0xffffffff ? 0x8000 : 0);
    equal(machine->run_interval(), 130);
    equal(cpu.read_control(Count), std::uint32_t(initial + 89));
    equal(callbacks, 1);
    video.connect_frame({});
  }
}

} // namespace test
