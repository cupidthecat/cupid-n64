#include "../fixture.hpp"
#include "core/system/console.hpp"

namespace test {
using namespace cupid::n64;

void cached_delay_slot_tests() {
  auto machine = std::make_unique<Console>();
  for (unsigned opcode : {40u, 41u, 43u, 63u, 57u, 61u})
    for (unsigned before : {0u, 1u, 7u})
      for (unsigned iterations : {1u, 2u, 4u})
        for (bool warm : {false, true}) {
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
          const unsigned step = opcode == 63 || opcode == 61 ? 8 : 4;
          std::vector<std::uint32_t> code(before, i(9, 10, 10, 1));
          code.push_back(i(9, 8, 8, step));
          code.push_back(i(5, 8, 9, 0xfffe));
          code.push_back(i(opcode, 8, 2, static_cast<std::uint16_t>(-step)));
          code.push_back(r(8, 31, 0, 0));
          code.push_back(0);
          auto ram = machine->ram().words();
          for (unsigned n = 0; n < code.size(); ++n)
            ram[0x1000 / 4 + n] = code[n];
          auto &cpu = machine->cpu();
          cpu.write_control(Status, 0x34000000);
          if (warm)
            for (unsigned line = 0; line < (code.size() + 7) / 8; ++line) {
              cpu.state().gpr[1] = 0xffffffff80001000ull + line * 32;
              cpu.execute(i(47, 1, 20, 0));
            }
          cpu.set_pc(0xffffffff80001000);
          cpu.write_control(Count, 0);
          cpu.write_control(Compare, 0);
          cpu.state().gpr[2] = 0x1122334455667788;
          cpu.state().fpr[2] = 0xaabbccddff00ee88;
          cpu.state().gpr[8] = 0xffffffffa4000000;
          cpu.state().gpr[9] = cpu.state().gpr[8] + step * iterations;
          cpu.state().gpr[31] = 0xffffffff80003000;
          const auto start = cpu.state().clocks;
          const auto fetched = before + 3;
          const auto initial = before * 2 + (warm ? 0 : 96 * ((fetched + 7) / 8));
          const auto stored = opcode == 40   ? 0x88000000u
                              : opcode == 41 ? 0x77880000u
                              : opcode == 43 ? 0x55667788u
                              : opcode == 63 ? 0x11223344u
                              : opcode == 57 ? 0xff00ee88u
                                             : 0xaabbccddu;
          for (unsigned call = 0; call <= iterations; ++call) {
            equal(cpu.run_block(start), true);
            const auto retired = std::min(call + 1, iterations);
            const auto clocks = initial + retired * 4 + (call == iterations ? 4 : 0);
            equal(cpu.state().clocks - start, clocks);
            equal(cpu.read_control(Count), clocks / 4);
            equal(cpu.state().pc, call == iterations ? 0xffffffff80003000ull
                                  : call + 1 == iterations
                                      ? 0xffffffff80001000ull + (before + 3) * 4
                                      : 0xffffffff80001000ull + before * 4);
            equal(cpu.state().gpr[8], 0xffffffffa4000000ull + step * retired);
            equal(cpu.state().gpr[10], before);
            equal(cpu.read_control(Cause), 0);
            equal(cpu.state().fcr31, 0);
            for (unsigned offset = 0; offset < 32; offset += 4)
              equal(machine->signal().read_word(offset),
                    offset < step * retired && offset % step == 0 ? stored : 0);
          }
        }
}

} // namespace test
