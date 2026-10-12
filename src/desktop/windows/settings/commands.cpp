#include "desktop/windows/commands.hpp"
#include "desktop/windows/dialogs/paths.hpp"
#include "desktop/windows/settings/transfer/review.hpp"
#include "desktop/windows/window.hpp"

namespace cupid::desktop {

void Window::apply_imported_settings() {
  const bool running = session_ != nullptr;
  end_rewind();
  stop_fast_forward();
  clear_host_input();
  gamepads_.stop();
  feedback_.clear();
  load_history_settings();
  load_display_settings();
  load_graphics_settings();
  shader_.select(read_shader_selection(settings_));
  load_playback_settings();
  load_gdb_settings();
  if (gdb_enabled_)
    gdb_.listen(gdb_port_);
  else
    gdb_.close();
  poll_gdb();
  const auto active = static_cast<LaunchFiles &>(options_);
  static_cast<LaunchFiles &>(options_) = {};
  load_launch_settings();
  launch_on_restart_ =
      running ? std::optional(static_cast<const LaunchFiles &>(options_)) : std::nullopt;
  if (running) {
    options_.rom = active.rom;
    options_.disk = active.disk;
    options_.arcade_profile = active.arcade_profile;
    options_.ipl = active.ipl.empty() ? std::filesystem::path{} : firmware_files_.ipl;
    options_.firmware = active.arcade_profile == n64::ArcadeProfile::Disabled
                            ? firmware_files_.n64
                            : firmware_files_.arcade;
  }
  session_settings_.ports = read_ports(settings_, controller_key_);
  player_mappings_ = read_mappings(settings_);
  shortcut_bindings_ = read_shortcuts(settings_);
  shortcuts_.bindings(shortcut_bindings_);
  load_recent();
  reload_pending_ = running;
  if (states_)
    states_->select(state_slot_);
  if (rewind_)
    rewind_->configure(rewind_settings_);
  apply_window_size();
  if (fullscreen_)
    position_fullscreen();
  if (running)
    refresh_frame();
  update_recent();
  refresh_audio_devices();
  refresh_monitors();
  update_states();
  update_playback();
  update_display();
  update_launch();
  update_controllers();
  update_graphics();
  update_shader();
  update_shortcuts();
  state_message_ = running ? L"Settings imported; hardware and storage changes apply on reload. "
                             L"Launch paths apply on next start."
                           : L"Settings imported";
  rebase();
}

bool Window::settings_command(unsigned id) {
  if (id != ImportSettings && id != ExportSettings)
    return false;
  clear_host_input();
  stop_fast_forward();
  try {
    const bool importing = id == ImportSettings;
    const auto directory = settings_.empty() ? application_directory() : settings_.parent_path();
    const auto chosen =
        select_file(window_, importing ? L"Import desktop settings" : L"Export desktop settings",
                    importing ? FileSelection::ImportSettings : FileSelection::ExportSettings,
                    directory, importing ? std::filesystem::path{} : L"cupid-settings.ini");
    if (chosen) {
      if (importing) {
        if (auto incoming = review_settings_paths(window_, SettingsTransfer::open(*chosen))) {
          incoming->import_to(settings_);
          apply_imported_settings();
        }
      } else {
        SettingsTransfer::capture(settings_).export_to(*chosen);
        state_message_ = L"Settings exported: " + chosen->filename().wstring();
      }
    }
  } catch (const std::exception &error) {
    status_error(id == ImportSettings ? L"Settings import failed" : L"Settings export failed",
                 error);
  }
  rebase();
  return true;
}

} // namespace cupid::desktop
