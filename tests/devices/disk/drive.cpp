#include "core/disk/drive.hpp"
#include "../fixture.hpp"
#include "core/disk/image/geometry.hpp"
#include "core/system/console.hpp"
#include <algorithm>

namespace test {
using namespace cupid::n64;
namespace {

struct DriveFixture {
  EventQueue events;
  DiskDrive drive{events, [] { return 1000000; }};
  bool interrupt = false;
  DriveFixture() {
    drive.connect_interrupt([this](bool line) { interrupt = line; });
    drive.power();
  }
  void advance(unsigned clocks) {
    events.advance(clocks, [&](Event event) { drive.event(event); });
  }
  std::uint16_t status() {
    return drive.read_register(8);
  }
  void command(unsigned command, unsigned parameter = 0) {
    drive.write_register(0, static_cast<std::uint16_t>(parameter));
    drive.write_register(8, static_cast<std::uint16_t>(command));
  }
};

std::vector<std::uint8_t> ipl() {
  std::vector<std::uint8_t> data(4096);
  data[0] = 0x80;
  data[1] = 0x27;
  data[2] = 7;
  data[3] = 0x40;
  std::copy_n("NDDJ", 4, data.begin() + 0x3b);
  return data;
}

void registers() {
  DriveFixture f;
  equal(f.status(), 0x59);
  equal(f.drive.read_register(20), 0x400);
  equal(f.drive.read_register(28), 0xc3);
  equal(f.drive.read_register(64), 3);
  f.command(27);
  equal(f.status() & 0x80, 0x80);
  equal(f.drive.read_register(0), 3);
  f.advance(7999);
  equal(f.interrupt, false);
  f.advance(1);
  equal(f.interrupt, true);
  equal(f.status() & 0x280, 0x200);
  f.drive.write_register(16, 0x100);
  equal(f.interrupt, false);
  f.command(9);
  f.advance(8000);
  equal(f.status() & 0x41, 0);
  f.command(1);
  equal(f.status() & 2, 2);
  f.advance(8000);
  f.command(10);
  equal(f.drive.read_register(0), 0x114);
  f.advance(8000);
  f.command(10, 1);
  equal(f.drive.read_register(0), 0x5300);
  f.advance(8000);
  f.command(0xeeee);
  equal(f.status() & 2, 2);
  f.drive.write_register(32, 0xaaab);
  equal(f.status() & 0x80, 0x80);
  f.drive.write_register(32, 0xaaaa);
  equal(f.status(), 0x59);
  equal(f.interrupt, false);
  auto firmware = ipl();
  equal(f.drive.load_ipl(firmware), true);
  equal(static_cast<unsigned>(f.drive.cic()), static_cast<unsigned>(CicModel::N8303));
  equal(f.drive.select(0x06000000, {}), true);
  equal(f.drive.read_half({}).value(), 0x8027);
  std::copy_n("NDXJ", 4, firmware.begin() + 0x3b);
  for (unsigned swap : {0u, 1u, 3u}) {
    std::vector<std::uint8_t> swapped(firmware.size());
    for (unsigned byte = 0; byte < swapped.size(); ++byte)
      swapped[byte] = firmware[byte ^ swap];
    equal(f.drive.load_ipl(swapped), true);
    equal(static_cast<unsigned>(f.drive.cic()), static_cast<unsigned>(CicModel::N8401));
    equal(f.drive.read_register(64), 4);
  }
  equal(f.drive.select(0x050005c0, {}), false);
  equal(f.drive.select(0x06400000, {}), false);
  f.drive.select(0x05000580, {});
  f.drive.write_half(0xabcd, {});
  f.drive.select(0x05000580, {});
  equal(f.drive.read_half({}).value(), 0xabcd);
  f.drive.power();
  f.drive.select(0x05000580, {});
  equal(f.drive.read_half({}).value(), 0);
}

void transfers() {
  DriveFixture f;
  std::vector<std::uint8_t> disk(disk_geometry::PhysicalSize);
  for (unsigned byte = 0; byte < 232; ++byte)
    disk[byte] = static_cast<std::uint8_t>(byte ^ 0xa5);
  std::vector<std::uint8_t> errors(4700);
  errors[0] = 1;
  equal(f.drive.insert(disk, errors), true);
  equal(f.drive.insert(f.drive.disk_data(), f.drive.disk_errors()), true);
  equal(f.drive.disk_errors()[0], 1);
  equal(f.drive.disk_data()[0], 0xa5);
  equal(f.status(), 0x159);
  f.command(1, 0x2000);
  equal(f.status() & 2, 2);
  f.advance(8000);
  f.command(2);
  equal(f.status() & 6, 6);
  f.advance(201692999);
  equal(f.status() & 0x80, 0x80);
  f.advance(1);
  equal(f.status() & 0x80, 0);
  equal(f.drive.read_register(12), 0x6000);
  f.drive.write_register(40, 231);
  f.drive.write_register(44, 0x54e7);
  equal(f.drive.read_register(48), 0x54e7);
  f.drive.write_register(16, 0xc100);
  f.advance(49999);
  equal(f.interrupt, false);
  f.advance(1);
  equal(f.interrupt, true);
  equal(f.drive.read_register(16) & 0x8060, 0x8060);
  equal(f.drive.read_register(28), 0x1c3);
  f.drive.select(0x05000400, {});
  for (unsigned byte = 0; byte < 232; byte += 2)
    equal(f.drive.read_half({}).value(), ((byte ^ 0xa5) << 8) | ((byte + 1) ^ 0xa5));
  equal(f.status() & 0x4400, 0x4400);
  equal(f.interrupt, false);
  for (unsigned sector = 1; sector <= 88; ++sector) {
    f.advance(38000);
    const auto status = f.status();
    equal(status & 0x400, 0x400);
    equal(status & 0x4000, sector < 85 ? 0x4000 : 0);
    equal(status & 0x1000, sector == 88 ? 0x1000 : 0);
  }
  equal(f.drive.read_register(16) & 0x8000, 0);
  f.advance(38000);
  equal(f.interrupt, false);
  f.drive.write_register(16, 0x8000);
  f.advance(50000);
  f.status();
  f.drive.select(0x05000400, {});
  for (unsigned byte = 0; byte < 232; byte += 2)
    f.drive.write_half(static_cast<std::uint16_t>((byte << 8) | (byte + 1)), {});
  f.advance(38000);
  for (unsigned byte = 0; byte < 232; ++byte)
    equal(f.drive.disk_data()[byte], byte);
  f.drive.eject();
  equal(f.status() & 0x900, 0x800);
  equal(f.drive.read_register(16) & 0x8400, 0x400);
  equal(f.drive.disk_data().empty(), true);
  f.drive.write_register(16, 0x1000);
  f.drive.write_register(16, 0);
  equal(f.drive.read_register(16) & 0x8400, 0);
  f.drive.power();
  equal(f.status(), 0x59);
}

void console_bus() {
  ConsoleConfig config;
  config.disk_drive = true;
  auto console = std::make_unique<Console>(config);
  std::array<std::uint8_t, 0x7c0> pif{};
  equal(console->load_disk(ipl(), pif), true);
  equal(console->read(0x06000000, 4).value, 0x80270740);
  console->write(0x05000508, 4, 27u << 16);
  console->cpu().state().clocks += 8000;
  console->synchronize();
  equal(console->cpu().read_control(Cause) & (1u << 11), 1u << 11);
  equal(console->read(0x05000500, 4).value, 3u << 16);
  console->write(0x05000510, 4, 0x100u << 16);
  equal(console->cpu().read_control(Cause) & (1u << 11), 0);
  console->power();
  equal(console->disk_drive().read_register(8), 0x59);
  equal(console->read(0x06000000, 4).value, 0x80270740);
  auto regular = std::make_unique<Console>();
  equal(regular->load_disk(ipl(), pif), false);
}

} // namespace

void disk_drive_tests() {
  registers();
  transfers();
  console_bus();
}

} // namespace test
