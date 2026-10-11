#pragma once

#include "desktop/session.hpp"
#include "desktop/state/slots.hpp"
#include "desktop/timing/playback_clock.hpp"
#include "desktop/windows/timing/timer_resolution.hpp"
#include "desktop/windows/video/paint_buffer.hpp"
#include <array>
#include <chrono>

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
    return paused_ || !active_ || modal_;
  }
  HWND window_ = nullptr;
  HMENU menu_ = nullptr;
  HMENU state_menu_ = nullptr;
  WINDOWPLACEMENT placement_{sizeof(WINDOWPLACEMENT)};
  Options options_;
  std::filesystem::path settings_;
  std::filesystem::path game_image_;
  TimerResolution timer_resolution_;
  PaintBuffer paint_buffer_;
  Audio audio_;
  std::unique_ptr<Session> session_;
  std::unique_ptr<StateSlots> states_;
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
