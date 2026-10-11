#include "desktop/windows/controls/preferences.hpp"
#include "core/state/archive.hpp"
#include "desktop/storage/save.hpp"
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <windows.h>

namespace cupid::desktop {
namespace {

void unicode_settings(const std::filesystem::path &path) {
  if (path.empty())
    return;
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
  auto bytes = std::filesystem::exists(path) ? read_file(path, 4 * 1024 * 1024)
                                             : std::vector<std::uint8_t>{};
  if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe)
    return;
  const bool utf8 = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf;
  if (utf8)
    bytes.erase(bytes.begin(), bytes.begin() + 3);
  std::wstring text;
  if (!bytes.empty()) {
    const auto codepage = utf8 ? CP_UTF8 : CP_ACP;
    const auto flags = utf8 ? MB_ERR_INVALID_CHARS : 0;
    const auto data = reinterpret_cast<const char *>(bytes.data());
    const auto length = static_cast<int>(bytes.size());
    const auto count = MultiByteToWideChar(codepage, flags, data, length, nullptr, 0);
    if (!count)
      throw std::runtime_error("Could not read the settings file encoding.");
    text.resize(static_cast<std::size_t>(count));
    if (MultiByteToWideChar(codepage, flags, data, length, text.data(), count) != count)
      throw std::runtime_error("Could not read the settings file encoding.");
  }
  std::vector<std::uint8_t> encoded{0xff, 0xfe};
  encoded.reserve(2 + text.size() * 2);
  for (const auto character : text) {
    encoded.push_back(static_cast<std::uint8_t>(character));
    encoded.push_back(static_cast<std::uint8_t>(character >> 8));
  }
  write_save(path, encoded);
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
}

class Preferences {
public:
  Preferences(const std::filesystem::path &path, std::wstring section)
      : path_(path), section_(std::move(section)) {}

  unsigned number(const std::wstring &key, unsigned fallback) const {
    return path_.empty()
               ? fallback
               : GetPrivateProfileIntW(section_.c_str(), key.c_str(), fallback, path_.c_str());
  }

  std::filesystem::path path(const std::wstring &key) const {
    std::array<wchar_t, 32768> value{};
    if (!path_.empty())
      GetPrivateProfileStringW(section_.c_str(), key.c_str(), L"", value.data(),
                               static_cast<DWORD>(value.size()), path_.c_str());
    return value.data();
  }

  void write(const std::wstring &key, const std::wstring &value) const {
    if (!path_.empty() &&
        !WritePrivateProfileStringW(section_.c_str(), key.c_str(), value.c_str(), path_.c_str()))
      throw std::runtime_error("Could not save controller settings. Check folder permissions.");
  }

  void write(const std::wstring &key, unsigned value) const {
    write(key, std::to_wstring(value));
  }

  InputBinding binding(const std::wstring &key, InputBinding fallback) const {
    const InputBinding value{
        static_cast<BindingKind>(number(key + L" Kind", static_cast<unsigned>(fallback.kind))),
        number(key + L" Code", fallback.code)};
    return valid_binding(value) ? value : fallback;
  }

  void write(const std::wstring &key, InputBinding value) const {
    write(key + L" Kind", static_cast<unsigned>(value.kind));
    write(key + L" Code", value.code);
  }

private:
  const std::filesystem::path &path_;
  std::wstring section_;
};

std::wstring port_key(unsigned port, const wchar_t *name) {
  return L"Port " + std::to_wstring(port + 1) + L" " + name;
}

} // namespace

std::wstring controller_settings_key(const std::filesystem::path &image) {
  if (image.empty())
    return {};
  std::wostringstream key;
  key << L"Controller ports " << std::hex << std::setfill(L'0') << std::setw(16)
      << n64::state::Archive::fingerprint(read_file(image, 0x0fc00000));
  return key.str();
}

ControllerPorts read_ports(const std::filesystem::path &settings, const std::wstring &key) {
  ControllerPorts ports;
  if (key.empty())
    return ports;
  Preferences preferences(settings, key);
  for (unsigned port = 0; port < ports.size(); ++port) {
    auto &value = ports[port];
    const auto controller = preferences.number(port_key(port, L"Controller"), 0);
    const auto accessory = preferences.number(port_key(port, L"Accessory"), 0);
    if (controller <= static_cast<unsigned>(ControllerType::GameCube))
      value.controller = static_cast<ControllerType>(controller);
    if (accessory <= static_cast<unsigned>(AccessoryType::BioSensor))
      value.accessory = static_cast<AccessoryType>(accessory);
    const auto bpm = preferences.number(port_key(port, L"Heart rate"), 60);
    value.heart_rate = bpm >= 30 && bpm <= 180 ? bpm : 60;
    value.cartridge = preferences.path(port_key(port, L"Cartridge"));
  }
  return ports;
}

void save_ports(const std::filesystem::path &settings, const std::wstring &key,
                const ControllerPorts &ports) {
  validate_ports(ports);
  if (key.empty())
    return;
  unicode_settings(settings);
  Preferences preferences(settings, key);
  for (unsigned port = 0; port < ports.size(); ++port) {
    const auto &value = ports[port];
    preferences.write(port_key(port, L"Controller"), static_cast<unsigned>(value.controller));
    preferences.write(port_key(port, L"Accessory"), static_cast<unsigned>(value.accessory));
    preferences.write(port_key(port, L"Heart rate"), value.heart_rate);
    preferences.write(port_key(port, L"Cartridge"), value.cartridge.wstring());
  }
}

PlayerMappings read_mappings(const std::filesystem::path &settings) {
  PlayerMappings mappings;
  for (unsigned port = 0; port < mappings.size(); ++port) {
    Preferences preferences(settings, L"Input mapping " + std::to_wstring(port + 1));
    const auto source = preferences.number(L"Source", port == 0 ? 1 : 0);
    auto &mapping = mappings[port];
    mapping = default_mapping(source <= static_cast<unsigned>(HostSource::Gamepad4)
                                  ? static_cast<HostSource>(source)
                                  : (port == 0 ? HostSource::Keyboard : HostSource::None));
    for (unsigned n = 0; n < mapping.buttons.size(); ++n)
      mapping.buttons[n] = preferences.binding(L"Button " + std::to_wstring(n), mapping.buttons[n]);
    for (unsigned n = 0; n < mapping.axes.size(); ++n) {
      auto &axis = mapping.axes[n];
      const auto key = L"Axis " + std::to_wstring(n);
      const auto selected = preferences.number(key, static_cast<unsigned>(axis.source));
      if (selected <= static_cast<unsigned>(HostAxis::RightTrigger))
        axis.source = static_cast<HostAxis>(selected);
      axis.inverted = preferences.number(key + L" Inverted", 0) != 0;
      axis.negative = preferences.binding(key + L" Negative", axis.negative);
      axis.positive = preferences.binding(key + L" Positive", axis.positive);
    }
    mapping.slow = preferences.binding(L"Walk", mapping.slow);
    mapping.feedback = preferences.number(L"Feedback", 1) != 0;
  }
  return mappings;
}

void save_mappings(const std::filesystem::path &settings, const PlayerMappings &mappings) {
  for (const auto &mapping : mappings)
    validate_mapping(mapping);
  unicode_settings(settings);
  for (unsigned port = 0; port < mappings.size(); ++port) {
    Preferences preferences(settings, L"Input mapping " + std::to_wstring(port + 1));
    const auto &mapping = mappings[port];
    preferences.write(L"Source", static_cast<unsigned>(mapping.source));
    for (unsigned n = 0; n < mapping.buttons.size(); ++n)
      preferences.write(L"Button " + std::to_wstring(n), mapping.buttons[n]);
    for (unsigned n = 0; n < mapping.axes.size(); ++n) {
      const auto &axis = mapping.axes[n];
      const auto key = L"Axis " + std::to_wstring(n);
      preferences.write(key, static_cast<unsigned>(axis.source));
      preferences.write(key + L" Inverted", unsigned(axis.inverted));
      preferences.write(key + L" Negative", axis.negative);
      preferences.write(key + L" Positive", axis.positive);
    }
    preferences.write(L"Walk", mapping.slow);
    preferences.write(L"Feedback", unsigned(mapping.feedback));
  }
}

} // namespace cupid::desktop
