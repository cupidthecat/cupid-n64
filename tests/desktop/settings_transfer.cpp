#include "../support/test.hpp"
#include "desktop/storage/save.hpp"
#include "desktop/windows/settings/transfer.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <windows.h>

using namespace cupid::desktop;

namespace {

struct Directory {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      (L"cupid-settings-transfer-" +
       std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
  Directory() {
    std::filesystem::create_directories(path);
  }
  ~Directory() {
    std::filesystem::remove_all(path);
  }
};

void native(const std::filesystem::path &path, const settings::Document &document) {
  write_save(path, settings::encode_document(document, false));
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
}

void bytes(const std::filesystem::path &path, std::string_view text) {
  write_save(path, {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
}

template <class Function> bool rejects(Function function) {
  try {
    function();
  } catch (const std::exception &) {
    return true;
  }
  return false;
}

settings::Document configured(const Directory &directory) {
  auto document = SettingsTransfer::capture(directory.path / L"not-created.ini").document();
  document[L"Audio"][L"Volume"] = L"170";
  document[L"Audio"][L"Balance"] = L"-75";
  document[L"Audio"][L"Latency"] = L"240";
  document[L"Audio"][L"Device"] = L"output-device-identity";
  document[L"Display"][L"Scale"] = L"1";
  document[L"Display"][L"Aspect"] = L"1";
  document[L"Display"][L"Width"] = L"1200";
  document[L"Display"][L"Height"] = L"900";
  document[L"Display"][L"FullscreenMonitor"] = L"\\\\.\\DISPLAY2";
  document[L"Graphics"][L"InternalScale"] = L"4";
  document[L"Graphics"][L"Supersampling"] = L"1";
  document[L"Graphics"][L"BypassVI"] = L"1";
  document[L"Playback"][L"Sync"] = L"1";
  document[L"Playback"][L"AudioSync"] = L"0";
  document[L"Playback"][L"VideoSync"] = L"1";
  document[L"Playback"][L"Focus"] = L"2";
  document[L"Rewind"][L"Enabled"] = L"1";
  document[L"Rewind"][L"Mute"] = L"0";
  document[L"Rewind"][L"Interval"] = L"120";
  document[L"Rewind"][L"Capacity"] = L"320";
  document[L"Rewind"][L"MemoryMB"] = L"1024";
  document[L"States"][L"Slot"] = L"10";
  document[L"Memory"][L"Expansion Pak"] = L"0";
  document[L"Memory"][L"Port 4"] = L"62";
  const auto shader = directory.path / L"日本語 filter.hlsl";
  document[L"Shader"][L"Preset"] = L"3";
  document[L"Shader"][L"File"] = shader.wstring();
  for (const auto key : {L"Saves", L"Screenshots", L"Firmware", L"Debugging"})
    document[L"Folders"][key] = (directory.path / (L"設定 " + std::wstring(key))).wstring();
  document[L"Firmware"][L"Nintendo64"] = (directory.path / L"firmware.bin").wstring();
  document[L"Launch"][L"ROM"] = (directory.path / L"game.z64").wstring();
  document[L"Launch"][L"Arcade"] = L"12";
  document[L"Recent ROMs"][L"1"] = document.at(L"Launch").at(L"ROM");
  document[L"Input mapping 1"][L"Button 0 Code"] = L"88";
  document[L"Input mapping 1"][L"Axis 0 Inverted"] = L"1";
  document[L"Input mapping 2"][L"Source"] = L"3";
  document[L"Input mapping 2"][L"Button 0 Kind"] = L"2";
  document[L"Input mapping 2"][L"Button 0 Code"] = L"4096";
  document[L"Shortcuts"][L"Action 0 Binding 0 Key"] = L"80";
  document[L"Shortcuts"][L"Action 0 Binding 0 Modifiers"] = L"3";
  for (const auto key :
       {L"Controller ports 0123456789abcdef", L"Controller ports fedcba9876543210"}) {
    document[key][L"Port 1 Controller"] = L"2";
    document[key][L"Port 1 Accessory"] = L"4";
    document[key][L"Port 1 Cartridge"] = (directory.path / L"日本語 game.gb").wstring();
    document[key][L"Port 4 Heart rate"] = L"180";
  }
  return settings::normalize(document, false);
}

void round_trip(const Directory &directory) {
  const auto source = directory.path / L"source.ini";
  const auto exported = directory.path / L"export.ini";
  const auto target = directory.path / L"target.ini";
  auto expected = configured(directory);
  auto original = expected;
  original[L"Unrelated"][L"Value"] = L"private-local-value";
  native(source, original);
  const auto capture = SettingsTransfer::capture(source);
  test::equal(capture.document() == expected, true);
  capture.export_to(exported);
  const auto incoming = SettingsTransfer::open(exported);
  test::equal(incoming.document() == expected, true);
  native(target, {{L"Unrelated", {{L"Value", L"target-value"}}},
                  {L"Controller ports aaaaaaaaaaaaaaaa", {{L"Port 1 Controller", L"4"}}}});
  incoming.import_to(target);
  test::equal(SettingsTransfer::capture(target).document() == expected, true);
  const auto imported = settings::read_document(target, false);
  test::equal(imported.at(L"Unrelated").at(L"Value") == L"target-value", true);
  test::equal(imported.contains(L"Controller ports aaaaaaaaaaaaaaaa"), false);
  const auto data = read_file(exported, settings::maximum_size);
  const std::string text(data.begin(), data.end());
  test::equal(text.find("private-local-value") == text.npos, true);
  test::equal(GetPrivateProfileIntW(L"Audio", L"Balance", 0, target.c_str()),
              static_cast<unsigned>(-75));
  std::array<wchar_t, 32768> path{};
  GetPrivateProfileStringW(L"Shader", L"File", L"", path.data(), static_cast<DWORD>(path.size()),
                           target.c_str());
  test::equal(std::wstring(path.data()) == expected.at(L"Shader").at(L"File"), true);
  test::equal(GetPrivateProfileIntW(L"Shortcuts", L"Action 0 Binding 0 Key", 0, target.c_str()),
              80);
  test::equal(GetPrivateProfileIntW(L"Input mapping 2", L"Source", 0, target.c_str()), 3);
  test::equal(GetPrivateProfileIntW(L"Launch", L"Arcade", 0, target.c_str()), 12);
  const auto before = read_file(source, settings::maximum_size);
  capture.export_to(exported);
  test::equal(read_file(source, settings::maximum_size) == before, true);
  for (const auto &[section, entries] : expected)
    for (const auto &[key, value] : entries)
      test::equal(incoming.document().at(section).at(key) == value, true);
}

void invalid(const Directory &directory) {
  const auto target = directory.path / L"invalid-target.ini";
  const auto exported = directory.path / L"invalid-export.ini";
  const auto valid = configured(directory);
  native(target, valid);
  const auto before = read_file(target, settings::maximum_size);
  const auto unchanged = [&] {
    test::equal(read_file(target, settings::maximum_size) == before, true);
    test::equal(GetPrivateProfileIntW(L"Audio", L"Volume", 0, target.c_str()), 170);
  };
  const auto reject = [&](const settings::Document &document) {
    write_save(exported, settings::encode_document(document, true));
    test::equal(rejects([&] { SettingsTransfer::open(exported).import_to(target); }), true);
    unchanged();
  };
  for (const auto &[section, key, value] : std::array{
           std::array{L"Audio", L"Volume", L"201"}, std::array{L"Audio", L"Balance", L"-101"},
           std::array{L"Audio", L"Latency", L"30"}, std::array{L"Display", L"Width", L"63"},
           std::array{L"Display", L"Height", L"8193"},
           std::array{L"Graphics", L"InternalScale", L"3"}, std::array{L"Graphics", L"Weave", L"1"},
           std::array{L"Shader", L"File", L""}, std::array{L"Playback", L"Focus", L"3"},
           std::array{L"Memory", L"Port 1", L"2"}, std::array{L"Rewind", L"Capacity", L"11"},
           std::array{L"States", L"Slot", L"0"},
           std::array{L"Input mapping 1", L"Button 0 Code", L"0"},
           std::array{L"Input mapping 2", L"Button 0 Code", L"3"},
           std::array{L"Input mapping 3", L"Source", L"6"},
           std::array{L"Shortcuts", L"Action 0 Binding 0 Key", L"16"},
           std::array{L"Controller ports 0123456789abcdef", L"Port 4 Heart rate", L"181"}}) {
    auto document = valid;
    document[section][key] = value;
    reject(document);
  }
  for (const auto value : {L"-1", L"100junk", L"+100", L"0x64", L"4294967396", L"", L"2.0"}) {
    auto document = valid;
    document[L"Audio"][L"Volume"] = value;
    reject(document);
  }
  auto duplicate = valid;
  duplicate[L"Shortcuts"][L"Action 2 Binding 0 Key"] = L"80";
  duplicate[L"Shortcuts"][L"Action 2 Binding 0 Modifiers"] = L"3";
  reject(duplicate);
  auto missing = valid;
  missing.at(L"Audio").erase(L"Volume");
  reject(missing);
  auto unknown = valid;
  unknown[L"Audio"][L"Future"] = L"1";
  reject(unknown);
  unknown = valid;
  unknown[L"Unknown"][L"Value"] = L"1";
  reject(unknown);
  for (const auto text :
       {"[Settings export]\nVersion=2\n", "[Audio]\nVolume=100\n",
        "[Settings export]\nVersion=1\n[Audio]\nVolume=100\nvolume=200\n",
        "[Settings export]\nVersion=1\n[Audio]\nVolume=100\n[audio]\nVolume=200\n",
        "[Settings export]\nVersion=1\nVolume\n"}) {
    bytes(exported, text);
    test::equal(rejects([&] { SettingsTransfer::open(exported).import_to(target); }), true);
    unchanged();
  }
  for (const auto &data :
       {std::vector<std::uint8_t>{0xff, 0xfe, 1}, std::vector<std::uint8_t>{0xff, 0xfe, 0, 0xd8},
        std::vector<std::uint8_t>{0xef, 0xbb, 0xbf, 0xff}, std::vector<std::uint8_t>{0}}) {
    write_save(exported, data);
    test::equal(rejects([&] { SettingsTransfer::open(exported).import_to(target); }), true);
    unchanged();
  }
  const auto blocked = directory.path / L"blocked";
  std::filesystem::create_directory(blocked);
  const auto incoming = SettingsTransfer::capture(target);
  test::equal(rejects([&] { incoming.import_to(blocked / L"absent" / L"settings.ini"); }), true);
  unchanged();
  const auto handle = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
  test::equal(handle != INVALID_HANDLE_VALUE, true);
  if (handle != INVALID_HANDLE_VALUE) {
    test::equal(rejects([&] { incoming.import_to(target); }), true);
    CloseHandle(handle);
    unchanged();
  }
  test::equal(rejects([&] { incoming.export_to(blocked); }), true);
}

void paths_and_data(const Directory &directory) {
  const auto source = directory.path / L"paths.ini";
  const auto exported = directory.path / L"paths-export.ini";
  auto document = configured(directory);
  native(source, document);
  auto incoming = SettingsTransfer::capture(source);
  const auto missing = incoming.missing_paths();
  test::equal(missing.size() >= 9, true);
  const auto replacement = directory.path / L"replacement.hlsl";
  bytes(replacement, "local shader content must not appear in export");
  for (const auto &field : missing)
    if (field.section == L"Shader") {
      incoming.path(field, replacement);
      const auto changed = incoming.document();
      test::equal(rejects([&] { incoming.path(field, {}); }), true);
      test::equal(incoming.document() == changed, true);
    }
  const auto remaining = incoming.missing_paths();
  test::equal(std::none_of(remaining.begin(), remaining.end(),
                           [](const auto &field) { return field.section == L"Shader"; }),
              true);
  for (const auto &field : remaining) {
    if (field.kind == settings::PathKind::Folder)
      std::filesystem::create_directories(field.path);
    else
      bytes(field.path, "private game firmware save contents __MEDIA_CONTENT_928371__");
  }
  test::equal(incoming.missing_paths().empty(), true);
  bytes(directory.path / L"game.eep", "private game save contents 11223344");
  incoming.export_to(exported);
  const auto data = read_file(exported, settings::maximum_size);
  const std::string text(data.begin(), data.end());
  for (const auto marker :
       {"private game", "__MEDIA_CONTENT_928371__", "11223344", "local shader content"})
    test::equal(text.find(marker) == text.npos, true);
  test::equal(SettingsTransfer::open(exported).document() == incoming.document(), true);
  native(source, {{L"Files", {{L"Firmware", L"legacy.bin"}, {L"ROM", L"legacy.z64"}}}});
  const auto legacy = SettingsTransfer::capture(source);
  test::equal(legacy.document().at(L"Firmware").at(L"Nintendo64") == L"legacy.bin", true);
  test::equal(legacy.document().at(L"Launch").at(L"ROM") == L"legacy.z64", true);
}

} // namespace

int main() {
  Directory directory;
  round_trip(directory);
  invalid(directory);
  paths_and_data(directory);
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
