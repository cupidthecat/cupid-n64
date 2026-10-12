#include "desktop/windows/dialogs/paths.hpp"
#include "desktop/windows/window.hpp"
#include <algorithm>
#include <array>

namespace cupid::desktop {
namespace {

constexpr std::array<n64::ArcadeProfile, 6> profiles{
    n64::ArcadeProfile::Standard,         n64::ArcadeProfile::ElevenBeat,
    n64::ArcadeProfile::StarSoldier,      n64::ArcadeProfile::HiPai,
    n64::ArcadeProfile::SuperRealMahjong, n64::ArcadeProfile::MagicalTetris};

LaunchFiles files(const PathForm &form) {
  LaunchFiles selected;
  selected.rom = form.fields[0].path;
  selected.firmware = form.fields[1].path;
  if (form.selected == 1) {
    selected.ipl = form.fields[2].path;
    selected.disk = form.fields[3].path;
  } else if (form.selected >= 2) {
    selected.arcade_profile = profiles.at(form.selected - 2);
  }
  return selected;
}

void absolute_paths(PathForm &form) {
  for (auto &field : form.fields)
    if (!field.path.empty())
      field.path = std::filesystem::absolute(field.path).lexically_normal();
}

} // namespace

void Window::launch_dialog() {
  PathForm form;
  form.title = L"Launch setup";
  form.directory =
      session_settings_.storage.directory(StorageLocation::Firmware, {}, application_directory());
  form.choices = {L"Nintendo 64 cartridge",        L"Nintendo 64 + 64DD",
                  L"Aleck64 - standard",           L"Aleck64 - Eleven Beat",
                  L"Aleck64 - Star Soldier",       L"Aleck64 - Hi Pai",
                  L"Aleck64 - Super Real Mahjong", L"Aleck64 - Magical Tetris"};
  if (options_.arcade_profile != n64::ArcadeProfile::Disabled) {
    const auto found = std::find(profiles.begin(), profiles.end(), options_.arcade_profile);
    form.selected = static_cast<unsigned>(found - profiles.begin()) + 2;
  } else {
    form.selected = options_.ipl.empty() ? 0 : 1;
  }
  form.fields = {{L"Cartridge", options_.rom, FileSelection::Cartridge},
                 {L"PIF firmware", options_.firmware, FileSelection::Pif},
                 {L"64DD IPL firmware", options_.ipl.empty() ? firmware_files_.ipl : options_.ipl,
                  FileSelection::Ipl},
                 {L"64DD disk", options_.disk, FileSelection::Disk}};
  const auto update = [](PathForm &model) {
    const bool drive = model.selected == 1;
    model.explanation =
        drive ? L"Required: PIF (1984 bytes) and 64DD IPL. Cartridge and disk are optional.\n"
                L"The drive can start empty; insert or change a disk from File."
              : L"Required: cartridge and PIF firmware.\n"
                L"Nintendo 64 PIF: 1984 bytes. Aleck64 PIF: 1984 or 2048 bytes.";
    model.fields[0].label = drive ? L"Cartridge (optional)" : L"Cartridge (required)";
    model.fields[1].label = L"PIF firmware (required)";
    model.fields[2].label = L"64DD IPL firmware (required for 64DD)";
    model.fields[3].label = L"64DD disk (optional)";
    model.fields[2].enabled = model.fields[3].enabled = drive;
  };
  form.changed = [this, update](PathForm &model) {
    model.fields[1].path = model.selected >= 2 ? firmware_files_.arcade : firmware_files_.n64;
    update(model);
  };
  update(form);
  form.validate = [](const PathForm &model) { validate_launch(files(model)); };
  if (auto selected = choose_paths(window_, std::move(form))) {
    absolute_paths(*selected);
    auto previous = options_;
    static_cast<LaunchFiles &>(options_) = files(*selected);
    try {
      load();
    } catch (...) {
      options_ = std::move(previous);
      throw;
    }
  }
}

void Window::firmware_dialog() {
  PathForm form;
  form.title = L"Local firmware settings";
  form.explanation = L"Nintendo 64 / 64DD PIF: 1984 bytes. Aleck64 PIF: 1984 or 2048 bytes.\n"
                     L"64DD also needs an IPL image. Blank entries leave that mode unconfigured.\n"
                     L"Firmware changes take effect when you reload or open a game.";
  form.directory =
      session_settings_.storage.directory(StorageLocation::Firmware, {}, application_directory());
  form.fields = {{L"Nintendo 64 and 64DD PIF", firmware_files_.n64, FileSelection::Pif},
                 {L"Aleck64 PIF", firmware_files_.arcade, FileSelection::Pif},
                 {L"64DD IPL", firmware_files_.ipl, FileSelection::Ipl}};
  form.validate = [](const PathForm &model) {
    constexpr std::array types{FirmwareType::Nintendo64, FirmwareType::Aleck64,
                               FirmwareType::DiskIpl};
    for (unsigned n = 0; n < types.size(); ++n)
      if (!model.fields[n].path.empty())
        validate_firmware(model.fields[n].path, types[n]);
  };
  if (auto selected = choose_paths(window_, std::move(form))) {
    absolute_paths(*selected);
    firmware_files_ = {selected->fields[0].path, selected->fields[1].path,
                       selected->fields[2].path};
    options_.firmware = options_.arcade_profile == n64::ArcadeProfile::Disabled
                            ? firmware_files_.n64
                            : firmware_files_.arcade;
    if (!options_.ipl.empty())
      options_.ipl = firmware_files_.ipl;
    if (launch_on_restart_ && !launch_on_restart_->ipl.empty())
      launch_on_restart_->ipl = firmware_files_.ipl;
    reload_pending_ |= session_ != nullptr;
    save_launch_settings();
  }
}

void Window::folders_dialog() {
  PathForm form;
  form.title = L"Output and firmware folders";
  const auto application = application_directory();
  form.explanation =
      L"Clear a field to use its default. Saves and memory changes apply on reload.\n"
      L"Defaults: saves/screenshots beside the game; debugging beside the capture;\n"
      L"firmware in the application folder: " +
      application.wstring();
  form.directory = application;
  constexpr std::array<const wchar_t *, 4> names{L"Game saves and save states", L"Screenshots",
                                                 L"Firmware picker", L"Debugging reports"};
  for (unsigned n = 0; n < names.size(); ++n)
    form.fields.push_back({names[n],
                           session_settings_.storage.selected(static_cast<StorageLocation>(n)),
                           FileSelection::Folder});
  form.validate = [](const PathForm &model) {
    for (unsigned n = 0; n < model.fields.size(); ++n)
      if (!model.fields[n].path.empty())
        validate_directory(model.fields[n].path, n != 2);
  };
  if (auto selected = choose_paths(window_, std::move(form))) {
    absolute_paths(*selected);
    const bool changed = session_settings_.storage.saves != selected->fields[0].path;
    for (unsigned n = 0; n < selected->fields.size(); ++n)
      session_settings_.storage.selected(static_cast<StorageLocation>(n)) =
          selected->fields[n].path;
    reload_pending_ |= changed && session_ != nullptr;
    save_launch_settings();
  }
}

} // namespace cupid::desktop
