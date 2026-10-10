#include "../support/test.hpp"
#include "core/disk/image/geometry.hpp"
#include "core/state/archive.hpp"
#include "core/state/core_state.hpp"
#include <algorithm>
#include <memory>

namespace {

using namespace cupid::n64;

struct DiskMachine {
  std::unique_ptr<Console> console;

  DiskMachine() {
    ConsoleConfig config;
    config.random_seed = 0;
    config.disk_drive = true;
    config.disk_clock = [] { return std::int64_t(946684800); };
    console = std::make_unique<Console>(config);
    std::array<std::uint8_t, 0x7c0> firmware{};
    std::array<std::uint8_t, 4096> ipl{};
    ipl[0] = 0x80;
    ipl[1] = 0x27;
    ipl[2] = 7;
    ipl[3] = 0x40;
    std::copy_n("NDDJ", 4, ipl.begin() + 0x3b);
    test::equal(console->load_disk(ipl, firmware), true);
    std::vector<std::uint8_t> disk(disk_geometry::PhysicalSize);
    for (unsigned byte = 0; byte < 512; ++byte)
      disk[byte] = static_cast<std::uint8_t>(byte * 37u ^ 0xa5u);
    std::array<std::uint8_t, 4700> errors{};
    errors[0] = 1;
    test::equal(console->disk_drive().insert(disk, errors), true);
    console->cpu().write_control(Status, 0x30000000);
    console->cpu().write_control(Compare, 0x10000000);
  }

  void advance(unsigned clocks) {
    console->cpu().state().clocks += clocks;
    console->synchronize();
  }

  DiskDrive &drive() {
    return console->disk_drive();
  }

  void command(unsigned code, unsigned parameter = 0) {
    drive().write_register(0, static_cast<std::uint16_t>(parameter));
    drive().write_register(8, static_cast<std::uint16_t>(code));
  }
};

template <typename Continue> void continuation(DiskMachine &machine, Continue run) {
  const auto initial = CoreState::capture(*machine.console);
  run(machine);
  const auto expected = CoreState::capture(*machine.console);
  CoreState::restore(*machine.console, initial);
  test::equal(CoreState::capture(*machine.console) == initial, true);
  run(machine);
  test::equal(CoreState::capture(*machine.console) == expected, true);
  DiskMachine fresh;
  CoreState::restore(*fresh.console, initial);
  test::equal(CoreState::capture(*fresh.console) == initial, true);
  run(fresh);
  test::equal(CoreState::capture(*fresh.console) == expected, true);
}

void invalid_view(DiskMachine &machine) {
  test::equal(machine.drive().select(0x05000400, {}), true);
  for (unsigned half = 0; half < 17; ++half)
    static_cast<void>(machine.drive().read_half({}));
  const auto initial = CoreState::capture(*machine.console);
  std::vector<std::uint8_t> marker;
  const auto put = [&](unsigned value) {
    for (unsigned byte = 0; byte < 4; ++byte)
      marker.push_back(static_cast<std::uint8_t>(value >> (byte * 8)));
  };
  put(3);
  put(34);
  marker.push_back(1);
  put(0);
  put(0);
  put(0);
  const auto found = std::search(initial.begin(), initial.end(), marker.begin(), marker.end());
  test::equal(found != initial.end(), true);
  if (found == initial.end())
    return;
  const auto unique = std::search(found + marker.size(), initial.end(), marker.begin(),
                                  marker.end()) == initial.end();
  test::equal(unique, true);
  if (!unique)
    return;
  auto damaged = initial;
  damaged[static_cast<std::size_t>(found - initial.begin())] = 5;
  const auto checksum = state::Archive::fingerprint(std::span(damaged).first(damaged.size() - 8));
  for (unsigned byte = 0; byte < 8; ++byte)
    damaged[damaged.size() - 8 + byte] = static_cast<std::uint8_t>(checksum >> (byte * 8));
  bool rejected = false;
  try {
    CoreState::restore(*machine.console, damaged);
  } catch (const state::InvalidState &) {
    rejected = true;
  }
  test::equal(rejected, true);
  test::equal(CoreState::capture(*machine.console) == initial, true);
}

} // namespace

int main() {
  try {
    for (unsigned epoch : {0u, 0xfffffff0u}) {
      DiskMachine machine;
      machine.advance(epoch);
      machine.command(1);
      machine.advance(201692999);
      test::equal(machine.drive().read_register(8) & 0x80, 0x80);
      continuation(machine, [](DiskMachine &m) {
        m.advance(1);
        test::equal(m.drive().read_register(8) & 0x280, 0x200);
        test::equal(m.console->cpu().read_control(Cause) & (1u << 11), 1u << 11);
        m.drive().write_register(16, 0x100);
        test::equal(m.console->cpu().read_control(Cause) & (1u << 11), 0);
      });
      machine.drive().write_register(40, 231);
      machine.drive().write_register(44, 0x54e7);
      machine.drive().write_register(16, 0xc000);
      machine.advance(49999);
      continuation(machine, [](DiskMachine &m) {
        m.advance(1);
        test::equal(m.drive().read_register(16) & 0x8060, 0x8060);
        test::equal(m.drive().read_register(28), 0x1c3);
        test::equal(m.drive().select(0x05000400, {}), true);
        for (unsigned byte = 0; byte < 232; byte += 2) {
          const auto upper = static_cast<std::uint8_t>(byte * 37u ^ 0xa5u);
          const auto lower = static_cast<std::uint8_t>((byte + 1) * 37u ^ 0xa5u);
          test::equal(m.drive().read_half({}).value_or(0), (unsigned(upper) << 8) | lower);
        }
        test::equal(m.drive().read_register(8) & 0x4400, 0x4400);
        m.advance(38000);
        test::equal(m.drive().read_register(28), 0x2c3);
        test::equal(m.drive().read_register(8) & 0x4400, 0x4400);
      });
      test::equal(machine.drive().select(0x05000400, {}), true);
      static_cast<void>(machine.drive().read_half({}));
      continuation(machine, [](DiskMachine &m) {
        for (unsigned byte = 2; byte < 232; byte += 2) {
          const auto upper = static_cast<std::uint8_t>((232 + byte) * 37u ^ 0xa5u);
          const auto lower = static_cast<std::uint8_t>((233 + byte) * 37u ^ 0xa5u);
          test::equal(m.drive().read_half({}).value_or(0), (unsigned(upper) << 8) | lower);
        }
      });
      machine.drive().write_register(16, 0x1000);
      machine.drive().write_register(16, 0);
      continuation(machine, [](DiskMachine &m) {
        m.advance(38000);
        test::equal(m.drive().read_register(16) & 0x8000, 0);
        test::equal(m.drive().read_register(8) & 0x4400, 0);
      });
      machine.drive().write_register(16, 0x8001);
      test::equal(machine.drive().select(0x05000400, {}), true);
      for (unsigned half = 0; half < 116; ++half)
        machine.drive().write_half(static_cast<std::uint16_t>(0xb500 ^ (half * 79)), {});
      machine.advance(49999);
      continuation(machine, [](DiskMachine &m) {
        m.advance(1);
        for (unsigned half = 0; half < 116; ++half) {
          const auto value = static_cast<std::uint16_t>(0xb500 ^ (half * 79));
          test::equal(m.drive().disk_data()[half * 2], value >> 8);
          test::equal(m.drive().disk_data()[half * 2 + 1], value & 255);
        }
        test::equal(m.drive().read_register(28), 0x2c3);
        test::equal(m.drive().read_register(8) & 0x4400, 0x4400);
      });
      invalid_view(machine);
      std::cout << "Epoch " << epoch << ": " << test::checks << " checks, " << test::failures
                << " failures\n";
      if (test::failures)
        return 1;
    }
  } catch (const std::exception &error) {
    std::cerr << "Disk snapshot continuation rejected: " << error.what() << '\n';
    return 1;
  }
  return test::failures ? 1 : 0;
}
