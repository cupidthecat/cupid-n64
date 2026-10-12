#include "desktop/linux/settings/settings.hpp"
#include "../../support/test.hpp"
#include "../state/fixture.hpp"
#include "desktop/linux/text.hpp"
#include "host.hpp"

namespace {

template <class Action> bool rejects(Action action) {
  try {
    action();
    return false;
  } catch (const std::exception &) {
    return true;
  }
}

} // namespace

void linux_settings_tests() {
  using namespace cupid::desktop;
  using namespace cupid::desktop::linux_host;
  session_test::Directory directory;
  const auto path = directory.path / "settings.ini";
  auto settings = load_settings(path);
  test::equal(settings.audio.latency, 80);
  test::equal(settings.mappings[0].source == HostSource::Keyboard, true);
  const std::filesystem::path unicode(u8"café-日本.z64");
  const auto game = directory.path / unicode;
  settings.firmware = {game, directory.path / "arcade.bin", directory.path / "ipl.bin"};
  settings.session.storage = {directory.path, directory.path, directory.path, directory.path};
  settings.session.graphics = {4, true, false, true};
  settings.session.memory.expansion = false;
  settings.audio = {{175, -25}, 130, L"Sortie 日本"};
  settings.muted = true;
  settings.scale = DisplayScale::Integer;
  settings.aspect = DisplayAspect::Pixels;
  settings.sync = PlaybackSync::Video;
  settings.focus = FocusBehavior::AllowInput;
  settings.gdb_enabled = true;
  settings.gdb_port = 32123;
  settings.width = 1280;
  settings.height = 800;
  for (unsigned n = 0; n < settings.mappings.size(); ++n) {
    settings.session.ports[n] = {ControllerType::Nintendo64, AccessoryType::Transfer, game, 75 + n};
    settings.session.memory.pak_banks[n] = MemorySettings::capacities[n];
    settings.mappings[n] = default_mapping(static_cast<HostSource>(2 + n));
    settings.mappings[n].buttons[n] = {BindingKind::AxisNegative, n};
    settings.mappings[n].axes[n].inverted = true;
    settings.mappings[n].slow = {BindingKind::GamepadButton, 0x4000};
    settings.mappings[n].feedback = n % 2;
  }
  save_settings(path, settings);
  const auto restored = load_settings(path);
  test::equal(restored.session.ports == settings.session.ports, true);
  test::equal(restored.mappings == settings.mappings, true);
  test::equal(restored.session.graphics == settings.session.graphics, true);
  test::equal(restored.session.memory.pak_banks == settings.session.memory.pak_banks, true);
  test::equal(restored.session.memory.expansion, false);
  test::equal(restored.audio.levels == settings.audio.levels, true);
  test::equal(restored.audio.latency, 130);
  test::equal(restored.audio.device == settings.audio.device, true);
  test::equal(restored.firmware.n64 == game &&
                  restored.firmware.arcade == settings.firmware.arcade &&
                  restored.firmware.ipl == settings.firmware.ipl,
              true);
  for (const auto location : {StorageLocation::Saves, StorageLocation::Screenshots,
                              StorageLocation::Firmware, StorageLocation::Debugging})
    test::equal(restored.session.storage.selected(location) == directory.path, true);
  test::equal(restored.scale == settings.scale && restored.aspect == settings.aspect, true);
  test::equal(restored.sync == settings.sync && restored.focus == settings.focus, true);
  test::equal(restored.gdb_enabled && restored.gdb_port == 32123, true);
  test::equal(restored.width == 1280 && restored.height == 800, true);
  test::equal(restored.muted && restored.hardware, true);
  settings.hardware = false;
  test::equal(rejects([&] { save_settings(path, settings); }), true);
  settings.session.graphics = {};
  save_settings(path, settings);
  test::equal(load_settings(path).hardware, false);
  const auto save = restored.session.storage.save_image(game, 0x1234);
  test::equal(save.filename() == std::filesystem::path(u8"café-日本-1234.z64"), true);
  const auto pak = save_path(save, L".pak");
  test::equal(pak.filename() == std::filesystem::path(u8"café-日本-1234.pak"), true);
  test::equal(
      save_path(directory.path / std::filesystem::path(u8"café-日本.EEP"), L".eep").filename() ==
          std::filesystem::path(u8"café-日本.eep.eep"),
      true);
  write_save(pak, std::array<std::uint8_t, 3>{1, 7, 9});
  test::equal(read_file(pak, 3) == std::vector<std::uint8_t>({1, 7, 9}), true);
  test::equal(utf8(wide("é 日本")) == "é 日本", true);
  test::equal(utf8({}).empty() && wide({}).empty(), true);
  for (const std::string_view corrupt :
       {"[Video]\nRenderScale=3\n", "[Audio]\nLatency=31\n", "[Video]\nScale=-1\n",
        "[Port1]\nSource=999\n", "[Port1]\nHeartRate=0\n", "[Debugger]\nPort=65536\n",
        "[Audio]\nMuted=perhaps\n", "[broken"}) {
    write_save(path, {reinterpret_cast<const std::uint8_t *>(corrupt.data()), corrupt.size()});
    test::equal(rejects([&] { load_settings(path); }), true);
  }
  test::equal(rejects([&] { wide(std::string_view("\xff", 1)); }), true);
}
