#include "core/system/console.hpp"
#include "fixture.hpp"

namespace test {
using namespace cupid::n64;

void console_tests() {
  std::vector<std::uint8_t> rom(4096);
  rom[0] = 0x80;
  rom[1] = 0x37;
  rom[2] = 0x12;
  rom[3] = 0x40;
  rom[4] = 0x11;
  rom[5] = 0x22;
  rom[6] = 0x33;
  rom[7] = 0x44;
  CartridgeRom cartridge;
  for (unsigned swap : {0u, 1u, 3u}) {
    auto data = rom;
    for (unsigned n = 0; n < rom.size(); ++n)
      data[n] = rom[n ^ swap];
    equal(cartridge.load(data), true);
    equal(cartridge.select(0x10000004, {}), true);
    equal(*cartridge.read_half({}), 0x1122);
    equal(*cartridge.read_half({}), 0x3344);
    equal(cartridge.select(0x10000fff, {}), true);
    equal(cartridge.select(0x10001000, {}), false);
    equal(cartridge.select(0x08000000, {}), false);
  }
  auto invalid = rom;
  invalid[0] = 0;
  equal(cartridge.load(invalid), false);
  invalid = rom;
  invalid.pop_back();
  equal(cartridge.load(invalid), false);
  equal(cartridge.load(std::span(rom).first(8)), false);

  Console console({.random_seed = 0});
  std::array<std::uint8_t, 0x7c0> firmware{};
  auto instruction = [&](unsigned address, std::uint32_t value) {
    for (unsigned n = 0; n < 4; ++n)
      firmware[address + n] = static_cast<std::uint8_t>(value >> ((3 - n) * 8));
  };
  instruction(0, i(15, 0, 8, 0xa400));
  instruction(4, i(15, 0, 9, 0x240a));
  instruction(8, i(13, 9, 9, 42));
  instruction(12, i(43, 8, 9, 0));
  instruction(16, r(8, 8, 0, 0));
  equal(console.load(rom, std::span(firmware).first(8)), false);
  equal(console.load(rom, firmware), true);
  equal(console.read(0x10000000, 4).value, 0x80371240);
  equal(console.read(0x04300004, 4).value, 0x02020102);
  equal(console.read(0x04300005, 1).value, 0x0202);
  equal(console.read(0x04300006, 2).value, 0x02020102);
  equal(console.read(0x04300004, 4).clocks, 40);
  console.write(0x04400002, 1, 0x12);
  equal(console.video().read_word(0), 0x1200);
  console.write(0x04400000, 8, 0x000056789abcdef0);
  equal(console.video().read_word(0), 0x5678);
  console.power();
  for (unsigned n = 0; n < 7; ++n)
    console.step();
  equal(console.cpu().state().gpr[10], 42);
  equal(console.cpu().state().pc, 0xffffffffa4000004);
  equal(console.frozen(), false);
  equal(console.signal().read_local(0, 4), 0x240a002a);
  console.run_clocks(1000);
  equal(console.cpu().state().clocks >= 1000, true);
  console.write(0x0430000c, 4, 0x80);
  console.interrupts().raise(Interrupt::Video);
  equal(console.cpu().read_control(Cause) & 0x400, 0x400);
  console.write(0x04400010, 4, 0);
  equal(console.cpu().read_control(Cause) & 0x400, 0);

  console.power();
  console.signal().write_local(0, 4, 0x29000000);
  console.signal().write_local(4, 4, 0);
  console.signal().state().gpr[1] = 2;
  console.signal().execute(c(4, 1, 11));
  console.signal().state().gpr[1] = 0;
  console.signal().execute(c(4, 1, 8));
  console.signal().state().gpr[1] = 8;
  console.signal().execute(c(4, 1, 9));
  equal(console.interrupts().read_word(8) & 32, 32);
  console.power();
  console.cpu().set_pc(0xffffffff84000000);
  const auto initial_cause = console.cpu().read_control(Cause);
  console.step();
  equal(console.frozen(), true);
  equal(console.cpu().state().pc, 0xffffffff84000004);
  equal(console.cpu().read_control(Cause), initial_cause);
  const auto frozen_clock = console.cpu().state().clocks;
  console.step();
  equal(console.cpu().state().clocks, frozen_clock + 2);
  equal(console.cpu().state().pc, 0xffffffff84000004);
  console.power();
  console.cpu().set_pc(0xffffffffa4000000);
  console.cpu().state().gpr[1] = 0xffffffff84000000;
  console.cpu().state().gpr[2] = 0xffffffff;
  console.cpu().execute(i(35, 1, 2, 0));
  equal(console.frozen(), true);
  equal(console.cpu().state().gpr[2], 0);
  equal(console.cpu().state().pc, 0xffffffffa4000004);
  console.power();
  equal(console.read(0x04400000, 8).value, 0);
  equal(console.frozen(), true);
  console.power();
  console.write(0x040c0000, 4, 0);
  equal(console.frozen(), true);
  console.power();
  console.read(0x04900000, 4);
  equal(console.frozen(), true);
  console.power();
  console.read(0x80000000, 4);
  equal(console.frozen(), true);
  console.power();
  console.write(0x04700008, 4, 0);
  console.write(0x0470000c, 4, 0x14);
  console.write(0x04300000, 4, 0x10f);
  console.write(0x03f80008, 4, 0x00080008);
  for (unsigned chip = 0; chip < 4; ++chip) {
    console.write(0x03f0000c, 4, 0x02000000);
    console.write(0x03f00004, 4, (chip + 4) * 2 << 26);
  }
  for (unsigned chip = 0; chip < 4; ++chip)
    console.write(0x03f00004 + (chip + 4) * 0x800, 4, chip * 2 << 26);
  console.write(0x1000, 8, 0x1122334455667788);
  equal(console.read(0x1000, 8).value, 0x1122334455667788);
  console.write(0x1000, 4, i(15, 0, 1, 0xa440));
  console.write(0x1004, 4, i(35, 1, 2, 0x10));
  console.write(0x1008, 4, i(5, 0, 0, 1));
  console.write(0x100c, 4, i(9, 0, 3, 42));
  console.write(0x1010, 4, i(9, 0, 4, 7));
  console.cpu().set_pc(0xffffffff80001000);
  console.cpu().write_control(Compare, 0xffffffff);
  console.run_interval();
  equal(console.cpu().state().gpr[3], 42);
  equal(console.cpu().state().gpr[4], 0);
  equal(console.cpu().state().pc, 0xffffffff80001010);
  console.cpu().write_control(Status, 0x30008401);
  console.interrupts().write_word(12, 0x80);
  console.interrupts().raise(Interrupt::Video);
  const auto interrupt_clock = console.cpu().state().clocks;
  equal(console.run_interval(), 2);
  equal(console.cpu().state().clocks, interrupt_clock + 2);
  equal(console.cpu().read_control(Epc), 0xffffffff80001010);
  equal(console.cpu().state().pc, 0xffffffff80000180);
  console.write(0x1000, 8, 0x1122334455667788);
  std::array<std::uint32_t, 4> burst{};
  console.read_burst(0x1000, burst);
  equal(burst[0], 0x11223344);
  equal(burst[1], 0x55667788);
  console.cpu().power();
  console.cpu().write_control(Status, 0x30000000);
  console.write(0x1000, 4, i(35, 3, 4, 0));
  console.write(0x1004, 4, c(4, 5, Status));
  console.cpu().state().gpr[2] = 0xffffffff80001000ull;
  console.cpu().state().gpr[3] = 0xffffffff80006000ull;
  console.cpu().state().gpr[4] = 0x12345678;
  console.cpu().state().gpr[5] = 0x30000000;
  console.cpu().execute(i(0x2f, 2, 20, 0));
  console.cpu().set_pc(0xffffffff80001000ull);
  console.cpu().write_control(Count, 0);
  console.cpu().write_control(Compare, 0xffffffff);
  console.write(0x04300000, 4, 0x400);
  const auto lookup_clock = console.cpu().state().clocks;
  equal(console.cpu().run_block(lookup_clock + 4096), true);
  equal(console.cpu().state().clocks - lookup_clock, 82);
  equal(console.cpu().read_control(Count), 20);
  equal(console.cpu().state().pc, 0xffffffff80001004);
  equal(console.cpu().state().gpr[4], 0);
  equal(console.cpu().read_control(Cause), 0);
  equal(console.cpu().read_control(Epc), 0);
  equal(console.frozen(), true);
  console.step();
  equal(console.cpu().state().clocks - lookup_clock, 84);
  equal(console.cpu().state().pc, 0xffffffff80001004);
  console.read_burst(0x1000, burst);
  equal(console.frozen(), true);

  for (bool expansion : {false, true}) {
    ConsoleConfig config;
    config.random_seed = 0;
    config.expansion = expansion;
    config.eeprom_size = 512;
    config.sram_size = 32768;
    Console reset_console(config);
    equal(reset_console.load(rom, firmware), true);
    reset_console.write(0x04700008, 4, 0);
    reset_console.write(0x0470000c, 4, 0x14);
    reset_console.write(0x04300000, 4, 0x10f);
    reset_console.write(0x03f80008, 4, 0x00080008);
    const auto chips = expansion ? 4u : 2u;
    for (unsigned chip = 0; chip < chips; ++chip) {
      reset_console.write(0x03f0000c, 4, 0x02000000);
      reset_console.write(0x03f00004, 4, (chip + 4) * 2 << 26);
    }
    for (unsigned chip = 0; chip < chips; ++chip)
      reset_console.write(0x03f00004 + (chip + 4) * 0x800, 4, chip * 2 << 26);
    reset_console.write(0x1000, 8, 0x1122334455667788);
    reset_console.ram().hidden()[17] = 0xa5;
    reset_console.eeprom().data()[7] = 0x69;
    reset_console.sram().data()[11] = 0x73;
    reset_console.write(0x04700010, 4, 0x12345678);
    RandomGenerator expected_random;
    for (unsigned n = 0; n < chips * 2; ++n)
      expected_random();
    reset_console.signal().write_io(16, 1);
    equal(reset_console.signal().read_status(0), expected_random() & 0xfff);
    reset_console.signal().state().gpr[1] = 0x99;
    reset_console.signal().write_local(0, 4, 0x89abcdef);
    reset_console.cpu().state().gpr[8] = 0x123;
    reset_console.cpu().advance_clocks(42);
    reset_console.video().write_word(0, 0x1234);
    reset_console.audio().write_word(8, 1);
    reset_console.power(true);
    equal(reset_console.cpu().state().clocks, 0);
    equal(reset_console.cpu().state().pc, 0xffffffffbfc00000);
    equal(reset_console.cpu().state().gpr[8], 0);
    equal(reset_console.signal().state().gpr[1], 0);
    equal(reset_console.signal().read_local(0, 4), 0);
    equal(reset_console.signal().status().halted, true);
    equal(reset_console.signal().clocks(), 0);
    equal(reset_console.video().read_word(0), 0);
    equal(reset_console.audio().state().enable, false);
    equal(reset_console.ram().identity(), true);
    equal(reset_console.read(0x1000, 8).value, 0x1122334455667788);
    equal(reset_console.ram().hidden()[17], 0xa5);
    equal(reset_console.read(0x04700010, 4).value, 0x12345678);
    equal(reset_console.eeprom().data()[7], 0x69);
    equal(reset_console.sram().data()[11], 0x73);
    reset_console.signal().write_io(16, 1);
    equal(reset_console.signal().read_status(0), expected_random() & 0xfff);
    reset_console.power();
    equal(reset_console.ram().identity(), false);
    equal(reset_console.ram().hidden()[17], 0);
    equal(reset_console.read(0x04700010, 4).value, 0);
    equal(reset_console.eeprom().data()[7], 0x69);
    equal(reset_console.sram().data()[11], 0x73);
  }
}

} // namespace test
