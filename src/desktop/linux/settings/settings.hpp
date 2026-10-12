#pragma once

#include "desktop/audio.hpp"
#include "desktop/controls/mapping.hpp"
#include "desktop/launch/files.hpp"
#include "desktop/session/settings.hpp"
#include "desktop/timing/playback_settings.hpp"
#include "desktop/video/viewport.hpp"

namespace cupid::desktop::linux_host {

struct Settings {
  SessionSettings session;
  FirmwareFiles firmware;
  Audio::Settings audio;
  std::array<ControllerMapping, 4> mappings{
      default_mapping(HostSource::Keyboard), default_mapping(HostSource::None),
      default_mapping(HostSource::None), default_mapping(HostSource::None)};
  DisplayScale scale = DisplayScale::Fit;
  DisplayAspect aspect = DisplayAspect::Standard;
  PlaybackSync sync = PlaybackSync::Audio;
  FocusBehavior focus = FocusBehavior::Pause;
  bool muted = false, feedback = true, hardware = true;
  bool gdb_enabled = false;
  unsigned gdb_port = 2345;
  unsigned width = 960, height = 720;
  void validate() const;
};

std::filesystem::path settings_path();
Settings load_settings(const std::filesystem::path &path);
void save_settings(const std::filesystem::path &path, const Settings &settings);

} // namespace cupid::desktop::linux_host
