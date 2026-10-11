#include "desktop/windows/controls/preferences.hpp"
#include "../../support/test.hpp"
#include "desktop/storage/save.hpp"
#include <chrono>
#include <windows.h>

namespace {

using namespace cupid::desktop;

struct Directory {
  std::filesystem::path path;
  Directory() {
    auto number = std::chrono::steady_clock::now().time_since_epoch().count();
    do {
      path = std::filesystem::temp_directory_path() /
             ("cupid-controller-settings-" + std::to_string(number++));
    } while (!std::filesystem::create_directory(path));
  }
  ~Directory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

template <class Action> bool rejects(Action action) {
  try {
    action();
    return false;
  } catch (const std::exception &) {
    return true;
  }
}

void preferences(const std::filesystem::path &directory) {
  const auto settings = directory / "settings.ini";
  const auto first = directory / "first.z64", same = directory / "same.z64";
  write_save(first, std::vector<std::uint8_t>(4096, 1));
  write_save(same, std::vector<std::uint8_t>(4096, 1));
  const auto key = controller_settings_key(first);
  test::equal(!key.empty(), true);
  test::equal(controller_settings_key(same) == key, true);
  test::equal(read_ports(settings, key) == ControllerPorts{}, true);
  ControllerPorts ports;
  ports[0] = {ControllerType::Nintendo64, AccessoryType::Transfer, directory / "media.gb", 45};
  ports[1] = {ControllerType::GameCube, AccessoryType::None, {}, 60};
  ports[2] = {ControllerType::Mouse, AccessoryType::Automatic, {}, 120};
  ports[3] = {ControllerType::Disconnected, AccessoryType::BioSensor, {}, 180};
  save_ports(settings, key, ports);
  test::equal(read_ports(settings, key) == ports, true);
  const auto unicode = directory / "unicode.ini";
  WritePrivateProfileStringW(L"Existing", L"Value", L"preserved", unicode.c_str());
  auto unicode_ports = ports;
  unicode_ports[0].cartridge = directory / L"\u65e5\u672c\u8a9e\u30b2\u30fc\u30e0.gb";
  save_ports(unicode, key, unicode_ports);
  test::equal(read_ports(unicode, key) == unicode_ports, true);
  std::array<wchar_t, 32> existing{};
  GetPrivateProfileStringW(L"Existing", L"Value", L"", existing.data(),
                           static_cast<DWORD>(existing.size()), unicode.c_str());
  test::equal(std::wstring(existing.data()) == L"preserved", true);
  write_save(first, std::vector<std::uint8_t>(4096, 2));
  const auto second_key = controller_settings_key(first);
  test::equal(second_key != key, true);
  test::equal(read_ports(settings, second_key) == ControllerPorts{}, true);
  auto second_ports = ports;
  second_ports[0].accessory = AccessoryType::Memory;
  save_ports(settings, second_key, second_ports);
  test::equal(read_ports(settings, second_key) == second_ports, true);
  test::equal(read_ports(settings, key) == ports, true);
  auto mappings = read_mappings(settings);
  test::equal(mappings[0] == default_mapping(HostSource::Keyboard), true);
  for (unsigned port = 1; port < mappings.size(); ++port)
    test::equal(mappings[port] == default_mapping(HostSource::None), true);
  mappings[0].buttons[0] = {BindingKind::Key, 'B'};
  mappings[0].slow = {};
  mappings[1] = default_mapping(HostSource::Gamepad3);
  mappings[1].buttons[2] = {BindingKind::GamepadButton, 0x4000};
  mappings[1].axes[0] = {HostAxis::RightX, true, {}, {}};
  mappings[1].feedback = false;
  mappings[2] = default_mapping(HostSource::Gamepad4);
  mappings[3] = default_mapping(HostSource::Keyboard);
  mappings[3].buttons[0] = {BindingKind::Key, 'N'};
  save_mappings(settings, mappings);
  test::equal(read_mappings(settings) == mappings, true);
  test::equal(read_ports(settings, key) == ports, true);
  WritePrivateProfileStringW(key.c_str(), L"Port 1 Controller", L"99", settings.c_str());
  WritePrivateProfileStringW(key.c_str(), L"Port 2 Accessory", L"-1", settings.c_str());
  WritePrivateProfileStringW(key.c_str(), L"Port 4 Heart rate", L"181", settings.c_str());
  const auto malformed = read_ports(settings, key);
  test::equal(static_cast<unsigned>(malformed[0].controller),
              static_cast<unsigned>(ControllerType::Automatic));
  test::equal(static_cast<unsigned>(malformed[1].accessory),
              static_cast<unsigned>(AccessoryType::Automatic));
  test::equal(malformed[3].heart_rate, 60);
  test::equal(malformed[0].cartridge == ports[0].cartridge, true);
  WritePrivateProfileStringW(L"Input mapping 1", L"Button 0 Code", L"256", settings.c_str());
  WritePrivateProfileStringW(L"Input mapping 2", L"Axis 0", L"99", settings.c_str());
  WritePrivateProfileStringW(L"Input mapping 4", L"Source", L"99", settings.c_str());
  const auto invalid = read_mappings(settings);
  test::equal(invalid[0].buttons[0] == default_mapping(HostSource::Keyboard).buttons[0], true);
  test::equal(static_cast<unsigned>(invalid[1].axes[0].source),
              static_cast<unsigned>(HostAxis::LeftX));
  test::equal(static_cast<unsigned>(invalid[3].source), static_cast<unsigned>(HostSource::None));
  test::equal(!invalid[1].feedback, true);
  const auto saved = read_file(settings, 128 * 1024);
  auto bad_ports = ports;
  bad_ports[3].heart_rate = 29;
  test::equal(rejects([&] { save_ports(settings, key, bad_ports); }), true);
  auto bad_mappings = mappings;
  bad_mappings[2].buttons[0] = {BindingKind::Key, 256};
  test::equal(rejects([&] { save_mappings(settings, bad_mappings); }), true);
  test::equal(read_file(settings, 128 * 1024) == saved, true);
  test::equal(rejects([&] { save_ports(directory / "missing" / "settings.ini", key, ports); }),
              true);
}

void defaults() {
  const auto automatic = default_ports({});
  test::equal(static_cast<unsigned>(automatic[0].controller),
              static_cast<unsigned>(ControllerType::Nintendo64));
  for (unsigned port = 1; port < 4; ++port)
    test::equal(static_cast<unsigned>(automatic[port].controller),
                static_cast<unsigned>(ControllerType::Disconnected));
  ControllerPorts ports;
  ports[3].accessory = AccessoryType::BioSensor;
  ports[3].heart_rate = 120;
  auto selected = resolve_ports(ports, automatic);
  test::equal(static_cast<unsigned>(selected[3].controller),
              static_cast<unsigned>(ControllerType::Nintendo64));
  test::equal(static_cast<unsigned>(selected[3].accessory),
              static_cast<unsigned>(AccessoryType::BioSensor));
  test::equal(selected[3].heart_rate, 120);
  ports[3].controller = ControllerType::Disconnected;
  selected = resolve_ports(ports, automatic);
  test::equal(static_cast<unsigned>(selected[3].accessory),
              static_cast<unsigned>(AccessoryType::None));
  const auto cartridge = default_ports(
      {cupid::n64::ControllerAccessory::None, cupid::n64::ControllerAccessory::Memory,
       cupid::n64::ControllerAccessory::Rumble, cupid::n64::ControllerAccessory::Transfer});
  ports = {};
  ports[1].accessory = AccessoryType::None;
  ports[2].controller = ControllerType::Mouse;
  ports[3].controller = ControllerType::GameCube;
  selected = resolve_ports(ports, cartridge);
  test::equal(static_cast<unsigned>(selected[1].controller),
              static_cast<unsigned>(ControllerType::Nintendo64));
  for (unsigned port = 1; port < 4; ++port)
    test::equal(static_cast<unsigned>(selected[port].accessory),
                static_cast<unsigned>(AccessoryType::None));
}

} // namespace

int main() {
  Directory directory;
  preferences(directory.path);
  defaults();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures != 0;
}
