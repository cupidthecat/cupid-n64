#include "core/debug/inspection.hpp"
#include "../support/test.hpp"
#include "core/state/core_state.hpp"
#include "desktop/debug/format.hpp"
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace {

using namespace cupid::n64;
using test::equal;

template <typename Action> bool rejects(Action action) {
  try {
    action();
  } catch (const std::exception &) {
    return true;
  }
  return false;
}

std::unique_ptr<Console> machine(bool expansion = true) {
  ConsoleConfig config;
  config.random_seed = 136133134;
  config.rtc_clock = [] { return std::int64_t(946684800); };
  config.disk_clock = config.rtc_clock;
  config.expansion = expansion;
  config.eeprom_size = 512;
  config.sram_size = 32768;
  config.flash_model = FlashModel::Mx29l0000;
  auto console = std::make_unique<Console>(config);
  std::vector<std::uint8_t> cartridge(4096), pif(0x7c0);
  cartridge[0] = 0x80;
  cartridge[1] = 0x37;
  cartridge[2] = 0x12;
  cartridge[3] = 0x40;
  equal(console->load(cartridge, pif), true);
  console->power();
  console->write(0x04700008, 4, 0);
  console->write(0x0470000c, 4, 0x14);
  console->write(0x04300000, 4, 0x10f);
  console->write(0x03f80008, 4, 0x00080008);
  for (unsigned n = 0; n < (expansion ? 4u : 2u); ++n) {
    console->write(0x03f0000c, 4, 0x02000000);
    console->write(0x03f00004, 4, (n + 4) * 2 << 26);
  }
  for (unsigned n = 0; n < (expansion ? 4u : 2u); ++n)
    console->write(0x03f00004 + (n + 4) * 0x800, 4, n * 2 << 26);
  equal(console->ram().identity(), true);
  return console;
}

std::string value(const std::vector<DeviceProperty> &properties, std::string_view name) {
  const auto found = std::find_if(properties.begin(), properties.end(),
                                  [&](const auto &property) { return property.name == name; });
  if (found == properties.end())
    throw std::runtime_error("Missing inspected property");
  return found->value;
}

void snapshots() {
  for (bool expansion : {false, true}) {
    auto console = machine(expansion);
    console->connect_controller(0, true);
    console->connect_mouse(1);
    console->connect_gamecube_controller(2);
    console->connect_controller(3, false);
    console->signal().write_io(28, 0);
    console->video().write_word(8, 320);
    console->interrupts().raise(Interrupt::Audio);
    const auto before = CoreState::capture(*console);
    for (const auto &device : Inspection::devices()) {
      const auto first = Inspection::properties(*console, device.id);
      equal(first.empty(), false);
      equal(Inspection::properties(*console, device.id) == first, true);
      for (const auto &property : first)
        equal(!property.name.empty() && !property.value.empty(), true);
    }
    equal(value(Inspection::properties(*console, DeviceId::Rsp), "Semaphore") == "No", true);
    equal(value(Inspection::properties(*console, DeviceId::Vi), "VI_H_WIDTH") == "0x00000140",
          true);
    equal(value(Inspection::properties(*console, DeviceId::Mi), "AI interrupt pending") == "Yes",
          true);
    const auto ports = Inspection::properties(*console, DeviceId::Controllers);
    equal(value(ports, "Port 1 controller") == "Nintendo 64 controller", true);
    equal(value(ports, "Port 2 controller") == "Nintendo 64 mouse", true);
    equal(value(ports, "Port 3 controller") == "GameCube controller", true);
    equal(value(ports, "Port 4 controller") == "Disconnected", true);
    for (const auto &region : Inspection::regions(*console)) {
      equal(region.size != 0 && !region.name.empty() && !region.access.empty(), true);
      Inspection::read(*console, region.id, 0, std::min(region.size, 256u));
      equal(Inspection::read(*console, region.id, region.size, 0).empty(), true);
    }
    equal(CoreState::capture(*console) == before, true);
    equal(rejects([&] { Inspection::properties(*console, static_cast<DeviceId>(999)); }), true);
  }
}

void active_device_snapshots() {
  for (const auto device : {DeviceId::Vi, DeviceId::Ai, DeviceId::Si, DeviceId::Rdp}) {
    auto console = machine();
    console->run_interval(1);
    const auto before = CoreState::capture(*console);
    Inspection::properties(*console, device);
    equal(CoreState::capture(*console) == before, true);
  }
  auto console = machine();
  console->audio().write_word(0, 0x12340);
  console->audio().write_word(8, 1);
  console->audio().write_word(16, 1000);
  console->audio().write_word(20, 15);
  const auto audio = Inspection::properties(*console, DeviceId::Ai);
  equal(value(audio, "AI_DRAM_ADDRESS") == "0x00012340", true);
  equal(value(audio, "AI_CONTROL") == "0x00000001", true);
  equal(value(audio, "AI_DACRATE") == "0x000003E8", true);
  equal(value(audio, "AI_BITRATE") == "0x0000000F", true);
  auto &command = const_cast<DisplayCommandState &>(console->display().command());
  command.tmem_busy = 19;
  equal(value(Inspection::properties(*console, DeviceId::Rdp), "DPC_TMEM_BUSY") == "0x00000013",
        true);
  console->cpu().write_control(Count, 0xffffffff);
  console->cpu().advance_clocks(3);
  const auto before = CoreState::capture(*console);
  equal(value(Inspection::properties(*console, DeviceId::Cpu), "CP0 Count") == "0x00000000FFFFFFFF",
        true);
  equal(CoreState::capture(*console) == before, true);
  console->cpu().advance_clocks(1);
  equal(value(Inspection::properties(*console, DeviceId::Cpu), "CP0 Count") == "0x0000000000000000",
        true);
}

void disk_boot_snapshots() {
  for (const auto &boot : std::array{std::pair{"NDDJ", "8303"}, std::pair{"NDXJ", "8401"},
                                     std::pair{"NDDE", "DDUS"}}) {
    ConsoleConfig config;
    config.random_seed = 133134;
    config.disk_drive = true;
    config.disk_clock = [] { return std::int64_t(946684800); };
    auto console = std::make_unique<Console>(config);
    std::vector<std::uint8_t> ipl(4096), firmware(0x7c0);
    ipl[0] = 0x80;
    ipl[1] = 0x27;
    ipl[2] = 7;
    ipl[3] = 0x40;
    std::copy_n(boot.first, 4, ipl.begin() + 0x3b);
    equal(console->load_disk(ipl, firmware), true);
    const auto before = CoreState::capture(*console);
    equal(value(Inspection::properties(*console, DeviceId::Console), "CIC") == boot.second, true);
    equal(CoreState::capture(*console) == before, true);
    ipl[1] = 0x37;
    ipl[2] = 0x12;
    equal(console->load(ipl, firmware), true);
    equal(value(Inspection::properties(*console, DeviceId::Console), "CIC") == "6102", true);
  }
}

void edits() {
  auto console = machine();
  for (const auto &region : Inspection::regions(*console)) {
    const auto before = CoreState::capture(*console);
    const std::array<std::uint8_t, 3> bytes{0x12, 0x80, 0xff};
    equal(rejects([&] { Inspection::write(*console, region.id, region.size - 1, bytes); }), true);
    equal(rejects([&] { Inspection::read(*console, region.id, region.size - 1, 2); }), true);
    equal(rejects([&] { Inspection::read(*console, region.id, 0xffffffff, 2); }), true);
    equal(rejects(
              [&] { Inspection::write(*console, region.id, 0, std::vector<std::uint8_t>(4097)); }),
          true);
    equal(CoreState::capture(*console) == before, true);
    if (!region.writable) {
      equal(rejects([&] { Inspection::write(*console, region.id, 0, bytes); }), true);
      equal(CoreState::capture(*console) == before, true);
      continue;
    }
    const auto clocks = console->cpu().state().clocks;
    Inspection::write(*console, region.id, region.size - 3, bytes);
    equal(Inspection::read(*console, region.id, region.size - 3, 3) ==
              std::vector<std::uint8_t>(bytes.begin(), bytes.end()),
          true);
    equal(console->cpu().state().clocks, clocks);
  }
  const auto before = CoreState::capture(*console);
  equal(rejects([&] {
          Inspection::write(*console, static_cast<MemoryRegionId>(999), 0,
                            std::array<std::uint8_t, 1>{1});
        }),
        true);
  equal(rejects([&] { Inspection::read(*console, MemoryRegionId::DiskIpl, 0, 1); }), true);
  equal(CoreState::capture(*console) == before, true);
}

void cache() {
  auto console = machine();
  auto &cpu = console->cpu();
  console->ram().write(0x1000, 4, 0x11223344);
  cpu.write_control(Status, 0x30000000);
  cpu.state().gpr[1] = 0xffffffff80001000;
  cpu.execute(0x8c220000);
  cpu.state().gpr[2] = 0xaabbccdd;
  cpu.execute(0xac220000);
  const auto before = CoreState::capture(*console);
  equal(Inspection::read(*console, MemoryRegionId::Rdram, 0x1000, 4) ==
            std::vector<std::uint8_t>({0x11, 0x22, 0x33, 0x44}),
        true);
  equal(Inspection::read(*console, MemoryRegionId::DataCache, 0x1000, 4) ==
            std::vector<std::uint8_t>({0xaa, 0xbb, 0xcc, 0xdd}),
        true);
  equal(CoreState::capture(*console) == before, true);
  const auto clocks = cpu.state().clocks;
  Inspection::write(*console, MemoryRegionId::DataCache, 0x1001,
                    std::array<std::uint8_t, 2>{0x44, 0x55});
  Inspection::write(*console, MemoryRegionId::Rdram, 0x1000, std::array<std::uint8_t, 1>{0x99});
  equal(cpu.state().clocks, clocks);
  cpu.execute(0x8c220000);
  equal(static_cast<std::uint32_t>(cpu.state().gpr[2]), 0xaa4455dd);
  cpu.state().gpr[1] = 0xffffffffa0001000;
  cpu.execute(0x8c220000);
  equal(static_cast<std::uint32_t>(cpu.state().gpr[2]), 0x99223344);
  cpu.state().gpr[1] = 0xffffffff80001000;
  cpu.execute(0xbc390000);
  equal(console->ram().read(0x1000, 4), 0xaa4455dd);
  Inspection::write(*console, MemoryRegionId::DataCache, 0x1400, std::array<std::uint8_t, 1>{0x66});
  equal(Inspection::read(*console, MemoryRegionId::Rdram, 0x1400, 1)[0], 0x66);
}

void compiled_code() {
  for (bool native : {false, true}) {
    auto console = machine();
    auto &cpu = console->cpu();
    cpu.write_control(Status, 0x30000000);
    cpu.state().gpr[3] = 0x30000000;
    const std::array<std::uint8_t, 8> code{0x24, 0x02, 0, 7, 0x40, 0x83, 0x60, 0};
    Inspection::write(*console, MemoryRegionId::Rdram, 0x2000, code);
    const auto run = [&] {
      cpu.set_pc(0xffffffff80002000);
      const auto target = cpu.state().clocks + 10000;
      equal(native ? cpu.run_block(target) : cpu.run_interpreted_block(target), true);
    };
    run();
    equal(cpu.state().gpr[2], 7);
    Inspection::write(*console, MemoryRegionId::Rdram, 0x2003, std::array<std::uint8_t, 1>{9});
    cpu.set_pc(0xffffffff80002000);
    cpu.step();
    equal(cpu.state().gpr[2], 7);
    cpu.state().gpr[1] = 0xffffffff80002000;
    cpu.execute(0xbc300000);
    run();
    equal(cpu.state().gpr[2], 9);
    auto &rsp = console->signal();
    const std::array<std::uint8_t, 8> signal{0x24, 0x02, 0, 7, 0, 0, 0, 0x0d};
    Inspection::write(*console, MemoryRegionId::RspImem, 0, signal);
    const auto signal_run = [&] {
      rsp.write_status(0, 0);
      rsp.write_io(16, 5);
      rsp.elapse(128);
      if (native)
        rsp.run();
      else
        rsp.run_interpreted();
      equal(rsp.status().halted && rsp.status().broken, true);
    };
    signal_run();
    equal(rsp.state().gpr[2], 7);
    Inspection::write(*console, MemoryRegionId::RspImem, 3, std::array<std::uint8_t, 1>{9});
    signal_run();
    equal(rsp.state().gpr[2], 9);
  }
}

void formatting() {
  using namespace cupid::desktop;
  equal(inspection_address(" 0Xffffffff80001234 "), 0xffffffff80001234);
  for (const auto text : {"", "-1", "10000000000000000", "12q", "12 34"})
    equal(rejects([&] { inspection_address(text); }), true);
  equal(inspection_bytes("12 abCD\n00") == std::vector<std::uint8_t>({0x12, 0xab, 0xcd, 0}), true);
  for (const auto text : {"", "1", "1 2", "GG", "0x12"})
    equal(rejects([&] { inspection_bytes(text); }), true);
  auto console = machine();
  for (const auto &region : Inspection::regions(*console)) {
    const auto start = region.address.value_or(0);
    equal(inspection_offset(region, start), 0);
    equal(inspection_offset(region, start + region.size - 1), region.size - 1);
    equal(rejects([&] { inspection_offset(region, start + region.size); }), true);
    if (region.id == MemoryRegionId::DataCache)
      equal(inspection_offset(region, 0x80001234), 0x1234);
  }
  const auto dump = inspection_dump(0xffffffff80000000, std::array<std::uint8_t, 3>{0x41, 0, 0xff});
  equal(dump.find("FFFFFFFF80000000  41 00 FF ") == 0, true);
  equal(dump.find("|A..|") != std::string::npos, true);
}

} // namespace

int main() {
  try {
    snapshots();
    active_device_snapshots();
    disk_boot_snapshots();
    edits();
    cache();
    compiled_code();
    formatting();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
