#include "desktop/windows/window.hpp"
#include <algorithm>
#include <stdexcept>

namespace cupid::desktop {
namespace {

std::filesystem::path path_setting(const std::filesystem::path &file, const wchar_t *section,
                                   const wchar_t *name, const wchar_t *fallback = L"") {
  std::array<wchar_t, 32768> buffer{};
  GetPrivateProfileStringW(section, name, fallback, buffer.data(),
                           static_cast<DWORD>(buffer.size()), file.c_str());
  return buffer.data();
}

} // namespace

std::filesystem::path Window::application_directory() const {
  std::array<wchar_t, 32768> path{};
  const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (!size || size >= path.size())
    throw std::runtime_error("Could not find the application folder.");
  return std::filesystem::path(path.data()).parent_path();
}

void Window::load_launch_settings() {
  if (settings_.empty())
    return;
  const auto legacy_firmware = path_setting(settings_, L"Files", L"Firmware");
  firmware_files_.n64 =
      path_setting(settings_, L"Firmware", L"Nintendo64", legacy_firmware.c_str());
  firmware_files_.arcade = path_setting(settings_, L"Firmware", L"Aleck64");
  firmware_files_.ipl = path_setting(settings_, L"Firmware", L"64DD IPL");
  session_settings_.memory.expansion =
      GetPrivateProfileIntW(L"Memory", L"Expansion Pak", 1, settings_.c_str()) != 0;
  for (unsigned port = 0; port < 4; ++port) {
    const auto banks = GetPrivateProfileIntW(
        L"Memory", (L"Port " + std::to_wstring(port + 1)).c_str(), 1, settings_.c_str());
    if (std::find(MemorySettings::capacities.begin(), MemorySettings::capacities.end(), banks) !=
        MemorySettings::capacities.end())
      session_settings_.memory.pak_banks[port] = banks;
  }
  constexpr std::array<const wchar_t *, 4> names{L"Saves", L"Screenshots", L"Firmware",
                                                 L"Debugging"};
  for (unsigned n = 0; n < names.size(); ++n)
    session_settings_.storage.selected(static_cast<StorageLocation>(n)) =
        path_setting(settings_, L"Folders", names[n]);
  if (options_.rom.empty() && options_.ipl.empty() && !options_.frames &&
      options_.arcade_profile == n64::ArcadeProfile::Disabled) {
    options_.rom = path_setting(settings_, L"Launch", L"ROM");
    options_.ipl = path_setting(settings_, L"Launch", L"IPL");
    options_.disk = path_setting(settings_, L"Launch", L"Disk");
    const auto arcade = GetPrivateProfileIntW(L"Launch", L"Arcade", 0, settings_.c_str());
    if (arcade <= static_cast<unsigned>(n64::ArcadeProfile::MagicalTetris))
      options_.arcade_profile = static_cast<n64::ArcadeProfile>(arcade);
    if (options_.rom.empty() && options_.ipl.empty())
      options_.rom = path_setting(settings_, L"Files", L"ROM");
  }
  if (options_.firmware.empty())
    options_.firmware = options_.arcade_profile == n64::ArcadeProfile::Disabled
                            ? firmware_files_.n64
                            : firmware_files_.arcade;
  else if (options_.arcade_profile == n64::ArcadeProfile::Disabled)
    firmware_files_.n64 = options_.firmware;
  else
    firmware_files_.arcade = options_.firmware;
  if (!options_.ipl.empty())
    firmware_files_.ipl = options_.ipl;
}

void Window::save_launch_settings() {
  if (settings_.empty() || options_.frames)
    return;
  const auto write = [&](const wchar_t *section, const wchar_t *name, const wchar_t *value) {
    if (!WritePrivateProfileStringW(section, name, value, settings_.c_str()))
      throw std::runtime_error(
          "Could not save launch or folder settings. Check folder permissions.");
  };
  write(L"Firmware", L"Nintendo64", firmware_files_.n64.c_str());
  write(L"Firmware", L"Aleck64", firmware_files_.arcade.c_str());
  write(L"Firmware", L"64DD IPL", firmware_files_.ipl.c_str());
  write(L"Launch", L"ROM", options_.rom.c_str());
  write(L"Launch", L"IPL", options_.ipl.c_str());
  write(L"Launch", L"Disk", options_.disk.c_str());
  write(L"Launch", L"Arcade",
        std::to_wstring(static_cast<unsigned>(options_.arcade_profile)).c_str());
  write(L"Memory", L"Expansion Pak", session_settings_.memory.expansion ? L"1" : L"0");
  for (unsigned port = 0; port < 4; ++port)
    write(L"Memory", (L"Port " + std::to_wstring(port + 1)).c_str(),
          std::to_wstring(session_settings_.memory.pak_banks[port]).c_str());
  constexpr std::array<const wchar_t *, 4> names{L"Saves", L"Screenshots", L"Firmware",
                                                 L"Debugging"};
  for (unsigned n = 0; n < names.size(); ++n)
    write(L"Folders", names[n],
          session_settings_.storage.selected(static_cast<StorageLocation>(n)).c_str());
}

std::filesystem::path Window::debugging_path(const wchar_t *extension) const {
  if (!session_settings_.storage.debugging.empty())
    validate_directory(session_settings_.storage.debugging, true);
  const auto folder = session_settings_.storage.directory(
      StorageLocation::Debugging, std::filesystem::absolute(options_.capture),
      application_directory());
  auto path = folder / options_.capture.filename();
  path += extension;
  return path;
}

} // namespace cupid::desktop
