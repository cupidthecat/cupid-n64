#pragma once

#include "desktop/debug/gdb/server.hpp"
#include "desktop/launch/options.hpp"
#include "desktop/linux/controls/input.hpp"
#include "desktop/linux/settings/settings.hpp"
#include "desktop/session.hpp"
#include "desktop/state/slots.hpp"
#include "desktop/timing/playback_clock.hpp"
#include <gtk/gtk.h>

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
  void refresh();
  void screenshot(const std::filesystem::path &path);
  void poll_gdb();
  bool tick();
  bool stopped() const;
  PlayerInputs inputs();
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
  int result_ = 0;
};

} // namespace cupid::desktop::linux_host
