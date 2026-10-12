#include "../support/test.hpp"
#include "core/state/archive.hpp"
#include "desktop/launch/files.hpp"
#include "desktop/state/slots.hpp"
#include "state/fixture.hpp"
#include <algorithm>

namespace {

using namespace cupid::desktop;

template <class Action> bool rejects(Action action) {
  try {
    action();
    return false;
  } catch (const std::exception &) {
    return true;
  }
}

void firmware(const std::filesystem::path &directory) {
  const auto pif = directory / "pif.bin", ipl = directory / "game.z64";
  validate_firmware(pif, FirmwareType::Nintendo64);
  validate_firmware(pif, FirmwareType::Aleck64);
  validate_firmware(ipl, FirmwareType::DiskIpl);
  LaunchFiles launch{ipl, pif, {}, {}};
  validate_launch(launch);
  launch.rom.clear();
  test::equal(rejects([&] { validate_launch(launch); }), true);
  launch.ipl = ipl;
  validate_launch(launch);
  launch.arcade_profile = cupid::n64::ArcadeProfile::Standard;
  test::equal(rejects([&] { validate_launch(launch); }), true);
  launch.ipl.clear();
  launch.rom = ipl;
  for (unsigned mode = 1; mode <= 6; ++mode) {
    launch.arcade_profile = static_cast<cupid::n64::ArcadeProfile>(mode);
    validate_launch(launch);
  }
  const auto other = directory / "other.bin";
  write_save(other, std::vector<std::uint8_t>(2048));
  validate_firmware(other, FirmwareType::Aleck64);
  test::equal(rejects([&] { validate_firmware(other, FirmwareType::Nintendo64); }), true);
  test::equal(rejects([&] { validate_firmware(other, FirmwareType::DiskIpl); }), true);
  write_save(other, std::vector<std::uint8_t>(1983));
  test::equal(rejects([&] { validate_firmware(other, FirmwareType::Aleck64); }), true);
  test::equal(rejects([&] { validate_firmware(directory / "missing", FirmwareType::Nintendo64); }),
              true);
}

void memory(const std::filesystem::path &directory) {
  const auto game = directory / "game.z64", pif = directory / "pif.bin";
  auto cartridge = read_file(game, 4096);
  std::copy_n("NWR", 3, cartridge.begin() + 0x3b);
  write_save(game, cartridge);
  Audio audio;
  audio.muted = true;
  Session normal(game, pif, audio, {}, {}, cupid::n64::ArcadeProfile::Disabled, 0, false);
  const auto initial = normal.capture_state();
  std::vector<std::uint8_t> old_pak(32 * 1024, 23);
  write_save(save_path(game, L".pak"), old_pak);
  SessionSettings settings;
  settings.memory.expansion = false;
  std::size_t reduced_size = 0;
  for (const auto banks : MemorySettings::capacities) {
    settings.memory.pak_banks[0] = banks;
    Session selected(game, pif, audio, {}, {}, cupid::n64::ArcadeProfile::Disabled, 0, false,
                     settings);
    const auto state = selected.capture_state();
    if (banks == 1)
      reduced_size = state.size();
    test::equal(state.size() + 4 * 1024 * 1024 < initial.size(), true);
    test::equal(state.size(), reduced_size + (banks - 1) * 32 * 1024);
    test::equal(rejects([&] { selected.restore_state(initial); }), true);
    test::equal(selected.capture_state() == state, true);
    selected.save();
    test::equal(read_file(save_path(game, L".pak"), old_pak.size()) == old_pak, true);
  }
  settings.memory.pak_banks[0] = 3;
  test::equal(rejects([&] {
                Session invalid(game, pif, audio, {}, {}, cupid::n64::ArcadeProfile::Disabled, 0,
                                false, settings);
              }),
              true);
  settings.memory.pak_banks[0] = 4;
  const auto large = save_path(game, L".128k.pak");
  write_save(large, old_pak);
  test::equal(rejects([&] {
                Session invalid(game, pif, audio, {}, {}, cupid::n64::ArcadeProfile::Disabled, 0,
                                false, settings);
              }),
              true);
  test::equal(read_file(large, old_pak.size()) == old_pak, true);
  std::filesystem::remove(large);
  settings.storage.saves = directory / "saves";
  std::filesystem::create_directory(settings.storage.saves);
  const auto routed =
      settings.storage.save_image(game, cupid::n64::state::Archive::fingerprint(cartridge));
  std::vector<std::uint8_t> larger_pak(128 * 1024, 17);
  write_save(save_path(routed, L".128k.pak"), larger_pak);
  Session selected(game, pif, audio, {}, {}, cupid::n64::ArcadeProfile::Disabled, 0, false,
                   settings);
  test::equal(selected.storage_image() == routed, true);
  StateSlots slots(
      selected.storage_image(), [&] { return selected.capture_state(); },
      [&](auto bytes) { selected.restore_state(bytes); });
  slots.save();
  test::equal(slots.path(1).parent_path().parent_path() == settings.storage.saves, true);
  test::equal(read_file(save_path(routed, L".128k.pak"), larger_pak.size()) == larger_pak, true);
  test::equal(read_file(save_path(game, L".pak"), old_pak.size()) == old_pak, true);
}

void disks(const std::filesystem::path &directory) {
  const auto pif = directory / "pif.bin", ipl = directory / "game.z64";
  std::vector<std::uint8_t> compact(0x4f08);
  compact[0x1c] = 0x80;
  compact[0xe2] = compact[0xe3] = compact[0xe4] = compact[0xe5] = 255;
  const auto first = directory / "first.d64", second = directory / "second.d64";
  write_save(first, compact);
  compact.back() = 33;
  write_save(second, compact);
  Audio audio;
  audio.muted = true;
  SessionSettings settings;
  settings.storage.saves = directory / "saves";
  Session session({}, pif, audio, ipl, first, cupid::n64::ArcadeProfile::Disabled, 0, false,
                  settings);
  test::equal(session.has_disk_drive(), true);
  test::equal(session.disk_image() == first, true);
  const auto initial = session.capture_state();
  const auto clocks = session.clocks();
  test::equal(rejects([&] { session.change_disk(directory / "missing"); }), true);
  test::equal(rejects([&] { session.change_disk(pif); }), true);
  test::equal(session.capture_state() == initial, true);
  session.change_disk(second);
  test::equal(session.disk_image() == second, true);
  test::equal(session.clocks(), clocks);
  test::equal(rejects([&] { session.restore_state(initial); }), true);
  auto second_save = save_path(session.storage_image(), L".disk");
  session.eject_disk();
  test::equal(session.disk_image().empty(), true);
  test::equal(session.clocks(), clocks);
  const auto empty = session.capture_state();
  session.restore_state(empty);
  test::equal(session.capture_state() == empty, true);
  write_save(second_save, std::vector<std::uint8_t>(1));
  test::equal(rejects([&] { session.change_disk(second); }), true);
  test::equal(session.capture_state() == empty, true);
  std::filesystem::remove(second_save);
  session.change_disk(first);
  session.restore_state(initial);
  test::equal(session.capture_state() == initial, true);
}

} // namespace

int main() {
  try {
    session_test::Directory directory;
    session_test::media(directory.path);
    firmware(directory.path);
    memory(directory.path);
    disks(directory.path);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
