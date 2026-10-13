#pragma once

#include "desktop/debug/gdb/server.hpp"
#include "desktop/launch/options.hpp"
#include "desktop/linux/controls/input.hpp"
#include "desktop/linux/settings/settings.hpp"
#include "desktop/session.hpp"
#include "desktop/state/slots.hpp"
#include "desktop/timing/execution_output.hpp"
#include "desktop/timing/playback_clock.hpp"
#include <atomic>
#include <gtk/gtk.h>
#include <mutex>

namespace cupid::desktop::linux_host {

class Window {
public:
  explicit Window(Options options, std::filesystem::path settings = settings_path(),
                  std::optional<bool> hardware = {});
  ~Window();
  Window(const Window &) = delete;
  Window &operator=(const Window &) = delete;
  int run();

private:
  friend struct WindowTestAccess;
  enum class Command {
    Open,
    Firmware,
    Disk,
    Eject,
    Close,
    Quit,
    Settings,
    Controls,
    Pause,
    Advance,
    Reset,
    SaveState,
    LoadState,
    UndoSave,
    UndoLoad,
    Fullscreen,
    Screenshot
  };
  void create_menu(GtkWidget *box);
  GtkWidget *item(GtkWidget *menu, const char *text, Command command);
  void command(Command command);
  void settings_dialog();
  void mapping_dialog(unsigned port, ControllerMapping &mapping);
  std::filesystem::path choose_file(const char *title, bool folder = false, bool saving = false);
  void load(LaunchFiles files);
  void close_game();
  void close();
  void persist();
  void rebase();
  void clear_input();
  void refresh(bool current_frame = true);
  void screenshot(const std::filesystem::path &path);
  void poll_gdb();
  bool tick();
  bool synchronous_tick();
  void start_playback();
  void finish_playback();
  void drain_playback();
  void publish_inputs();
  PlayerInputs take_inputs();
  void cache_session();
  void refresh_frame(const n64::VideoFrame &frame);
  bool stopped() const;
  PlayerInputs inputs(bool scripted = true);
  void draw(cairo_t *context);
  void error(const std::exception &error);
  static gboolean timer(gpointer data);
  Options options_;
  std::filesystem::path settings_path_;
  Settings settings_;
  Audio audio_;
  Input input_;
  HostInputs host_;
  FeedbackLatch feedback_;
  std::unique_ptr<Session> session_;
  std::unique_ptr<StateSlots> states_;
  std::unique_ptr<GdbServer> gdb_ = std::make_unique<GdbServer>();
  std::string gdb_error_;
  PlaybackClock clock_;
  GtkWidget *window_ = nullptr, *drawing_ = nullptr, *status_ = nullptr;
  GtkWidget *pause_item_ = nullptr;
  unsigned timer_ = 0;
  std::vector<std::uint8_t> pixels_;
  unsigned frame_width_ = 0, frame_height_ = 0;
  bool paused_ = false, active_ = true, modal_ = false, fullscreen_ = false;
  bool gdb_connected_ = false, gdb_was_paused_ = false;
  std::array<bool, 5> shortcuts_pressed_{};
  std::int32_t mouse_x_ = 0, mouse_y_ = 0;
  double pointer_x_ = 0, pointer_y_ = 0;
  bool pointer_known_ = false, mouse_left_ = false, mouse_right_ = false;
  std::chrono::steady_clock::time_point last_frame_{};
  std::chrono::steady_clock::time_point next_audio_probe_{};
  bool failed_ = false, last_stopped_ = false;
  bool terminal_frames_ = false;
  int result_ = 0;
  struct CompletedField {
    unsigned frame = 0;
    std::uint64_t clocks = 0;
    n64::VideoFrame image;
    std::array<FeedbackState, 4> feedback{};
  };
  unsigned observed_frames_ = 0;
  bool hardware_rendering_ = false, debugger_attached_ = false, debugger_halted_ = false;
  std::array<ControllerType, 4> controllers_{};
  std::array<FeedbackState, 4> feedback_state_{};
  std::mutex input_mutex_;
  PlayerInputs worker_inputs_{};
  std::array<MouseMotion, 4> worker_mouse_{};
  std::atomic<bool> advancing_{false};
  std::atomic<std::uint64_t> refresh_count_{0};
  std::uint64_t observed_refreshes_ = 0;
  std::atomic<n64::HostControl> worker_control_{n64::HostControl::None};
  std::unique_ptr<ExecutionOutput<CompletedField>> completed_fields_;
  // Join execution before destroying the session and other captured members.
  ExecutionWorker execution_;
};

} // namespace cupid::desktop::linux_host
