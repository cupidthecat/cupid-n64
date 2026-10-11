#pragma once

#include "desktop/session.hpp"
#include "desktop/state/rewind.hpp"
#include "desktop/state/slots.hpp"
#include "desktop/storage/recent.hpp"
#include "desktop/timing/playback_clock.hpp"
#include "desktop/timing/playback_settings.hpp"
#include "desktop/video/viewport.hpp"
#include "desktop/windows/audio/devices.hpp"
#include "desktop/windows/display/size_dialog.hpp"
#include "desktop/windows/timing/timer_resolution.hpp"
#include "desktop/windows/video/paint_buffer.hpp"
#include <array>
#include <chrono>
#include <string_view>

namespace cupid::desktop {

struct Options {
  std::filesystem::path rom, firmware, capture, ipl, disk;
  unsigned frames = 0;
  bool test_input = false;
  n64::ArcadeProfile arcade_profile = n64::ArcadeProfile::Disabled;
};

class Window {
public:
  explicit Window(Options options);
  int run(HINSTANCE instance, int show);

private:
  static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT message(UINT message, WPARAM wparam, LPARAM lparam);
  void command(unsigned id);
  bool state_command(unsigned id);
  bool playback_command(unsigned id);
  bool playback_settings_command(unsigned id);
  void load_playback_settings();
  void save_playback_settings();
  void create_playback_settings_menu(HMENU emulation);
  void update_playback_settings();
  void refresh_audio_devices();
  bool wait_for_video();
  bool display_command(unsigned id);
  bool file_command(unsigned id);
  void load_recent();
  void save_recent();
  void update_recent();
  void open_rom(const std::filesystem::path &path);
  void drop_rom(WPARAM drop);
  void load_display_settings();
  void save_display_settings();
  void create_display_menu(HMENU view);
  void update_display();
  void refresh_monitors();
  void apply_window_size();
  void remember_window_size();
  void position_fullscreen();
  int status_height() const;
  void screenshot();
  void update_playback();
  void update_fast_forward();
  void stop_fast_forward();
  void record_history();
  void begin_rewind();
  void rewind_step();
  void end_rewind();
  void status_error(std::wstring_view action, const std::exception &error);
  void update_states();
  void state_loaded();
  void refresh_frame();
  void load();
  void paint();
  void advance();
  struct Input {
    std::uint16_t buttons;
    std::int8_t x, y;
  };
  Input input() const;
  void present();
  void rebase();
  void fullscreen();
  void capture();
  void error(const std::exception &exception);
  std::filesystem::path choose(bool firmware);
  bool stopped() const {
    return paused_ || modal_ || (!active_ && focus_behavior_ == FocusBehavior::Pause);
  }
  HWND window_ = nullptr;
  HMENU menu_ = nullptr;
  HMENU state_menu_ = nullptr;
  HMENU rewind_menu_ = nullptr;
  HMENU monitor_menu_ = nullptr;
  HMENU recent_menu_ = nullptr;
  HMENU audio_menu_ = nullptr, audio_device_menu_ = nullptr;
  std::vector<AudioDevice> audio_devices_;
  static constexpr std::array<unsigned, 8> audio_latencies{20, 40, 60, 80, 120, 160, 240, 320};
  PlaybackSync playback_sync_ = PlaybackSync::Audio;
  FocusBehavior focus_behavior_ = FocusBehavior::Pause;
  bool video_sync_available_ = true;
  std::chrono::steady_clock::time_point next_audio_check_{};
  RecentRoms recent_;
  struct Monitor {
    std::wstring device;
    RECT bounds, work;
    bool primary;
  };
  std::vector<Monitor> monitors_;
  static constexpr std::array<ViewportSize, 4> viewport_presets{
      ViewportSize{320, 240}, ViewportSize{640, 480}, ViewportSize{960, 720},
      ViewportSize{1280, 960}};
  ViewportSize viewport_size_;
  DisplayScale display_scale_ = DisplayScale::Fit;
  DisplayAspect display_aspect_ = DisplayAspect::Standard;
  std::wstring fullscreen_monitor_;
  WINDOWPLACEMENT placement_{sizeof(WINDOWPLACEMENT)};
  Options options_;
  std::filesystem::path settings_;
  std::filesystem::path game_image_;
  TimerResolution timer_resolution_;
  PaintBuffer paint_buffer_;
  Audio audio_;
  std::unique_ptr<Session> session_;
  std::unique_ptr<StateSlots> states_;
  std::unique_ptr<RewindHistory> rewind_;
  static constexpr std::array<unsigned, 6> rewind_intervals{20, 40, 60, 80, 100, 120};
  static constexpr std::array<unsigned, 6> rewind_capacities{10, 20, 40, 80, 160, 320};
  static constexpr std::array<unsigned, 3> rewind_memory{256, 512, 1024};
  RewindHistory::Settings rewind_settings_;
  bool rewind_enabled_ = false, rewind_mute_ = true, rewinding_ = false;
  bool rewind_was_muted_ = false;
  bool fast_forward_ = false, fast_forward_hold_ = false, fast_forward_toggle_ = false;
  std::chrono::steady_clock::time_point next_rewind_;
  unsigned state_slot_ = 1;
  std::wstring state_message_;
  std::array<bool, 256> keys_{};
  std::vector<std::uint8_t> pixels_;
  bool paused_ = false, active_ = true, modal_ = false, fullscreen_ = false, quit_ = false;
  bool frame_ready_ = false;
  int exit_code_ = 0;
  PlaybackClock playback_clock_;
  unsigned last_frame_ = 0, measured_frames_ = 0;
  unsigned pixel_width_ = 0, pixel_height_ = 0;
  std::chrono::steady_clock::time_point measured_time_;
};

} // namespace cupid::desktop
