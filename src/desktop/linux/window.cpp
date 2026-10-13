#include "desktop/linux/window.hpp"
#include "desktop/linux/text.hpp"
#include "desktop/video/bitmap.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace cupid::desktop::linux_host {

Window::Window(Options options, std::filesystem::path settings, std::optional<bool> hardware)
    : options_(std::move(options)), settings_path_(std::move(settings)),
      settings_(settings_path_.empty() ? Settings{} : load_settings(settings_path_)) {
  completed_fields_ = std::make_unique<ExecutionOutput<CompletedField>>(execution_);
  audio_.wait_interrupt([this] { return advancing_.load() && execution_.interrupted(); });
  if (hardware) {
    settings_.hardware = *hardware;
    if (!*hardware)
      settings_.session.graphics = {};
  }
  audio_.configure(settings_.audio);
  audio_.muted = settings_.muted;
}

Window::~Window() {
  if (timer_)
    g_source_remove(timer_);
  try {
    finish_playback();
  } catch (const std::exception &failure) {
    std::cerr << failure.what() << '\n';
  }
  gdb_->target(nullptr);
  input_.stop();
  if (session_)
    try {
      session_->save();
    } catch (const std::exception &failure) {
      std::cerr << failure.what() << '\n';
    }
  if (window_)
    gtk_widget_destroy(window_);
}

int Window::run() {
  window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window_), "Cupid-N64");
  gtk_window_set_default_size(GTK_WINDOW(window_), static_cast<int>(settings_.width),
                              static_cast<int>(settings_.height));
  const auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(window_), box);
  create_menu(box);
  drawing_ = gtk_drawing_area_new();
  gtk_widget_set_hexpand(drawing_, TRUE);
  gtk_widget_set_vexpand(drawing_, TRUE);
  gtk_box_pack_start(GTK_BOX(box), drawing_, TRUE, TRUE, 0);
  status_ = gtk_label_new("Open a game and select its PIF firmware to begin.");
  gtk_label_set_xalign(GTK_LABEL(status_), 0);
  gtk_widget_set_margin_start(status_, 8);
  gtk_widget_set_margin_end(status_, 8);
  gtk_box_pack_end(GTK_BOX(box), status_, FALSE, FALSE, 4);
  g_signal_connect(drawing_, "draw",
                   G_CALLBACK(+[](GtkWidget *, cairo_t *context, gpointer data) -> gboolean {
                     static_cast<Window *>(data)->draw(context);
                     return TRUE;
                   }),
                   this);
  g_signal_connect(window_, "delete-event",
                   G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer data) -> gboolean {
                     auto &self = *static_cast<Window *>(data);
                     try {
                       self.close();
                     } catch (const std::exception &failure) {
                       self.error(failure);
                     }
                     return TRUE;
                   }),
                   this);
  g_signal_connect(window_, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer data) {
                     auto &self = *static_cast<Window *>(data);
                     self.window_ = self.drawing_ = self.status_ = self.pause_item_ = nullptr;
                     if (gtk_main_level())
                       gtk_main_quit();
                   }),
                   this);
  const auto key = +[](GtkWidget *, GdkEventKey *event, gpointer data) -> gboolean {
    auto &self = *static_cast<Window *>(data);
    const bool pressed = event->type == GDK_KEY_PRESS;
    self.input_.key(event->keyval, pressed, event->hardware_keycode);
    constexpr std::array<unsigned, 5> keys{GDK_KEY_F11, GDK_KEY_F5, GDK_KEY_F7, GDK_KEY_F8,
                                           GDK_KEY_F9};
    constexpr std::array<Command, 5> actions{Command::Fullscreen, Command::SaveState,
                                             Command::LoadState, Command::Pause, Command::Advance};
    const auto found = std::find(keys.begin(), keys.end(), event->keyval);
    if (found == keys.end())
      return FALSE;
    const auto index = static_cast<unsigned>(found - keys.begin());
    const bool repeated = self.shortcuts_pressed_[index];
    self.shortcuts_pressed_[index] = pressed;
    if (!pressed || repeated)
      return TRUE;
    try {
      self.command(actions[index]);
    } catch (const std::exception &failure) {
      self.error(failure);
    }
    return TRUE;
  };
  g_signal_connect(window_, "key-press-event", G_CALLBACK(key), this);
  g_signal_connect(window_, "key-release-event", G_CALLBACK(key), this);
  const auto focus = +[](GtkWidget *, GdkEventFocus *event, gpointer data) -> gboolean {
    auto &self = *static_cast<Window *>(data);
    try {
      self.finish_playback();
    } catch (const std::exception &failure) {
      self.error(failure);
      return FALSE;
    }
    self.active_ = event->in;
    self.shortcuts_pressed_.fill(false);
    self.clear_input();
    self.audio_.clear();
    self.rebase();
    return FALSE;
  };
  g_signal_connect(window_, "focus-in-event", G_CALLBACK(focus), this);
  g_signal_connect(window_, "focus-out-event", G_CALLBACK(focus), this);
  gtk_widget_add_events(drawing_, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK |
                                      GDK_BUTTON_RELEASE_MASK | GDK_LEAVE_NOTIFY_MASK);
  g_signal_connect(drawing_, "motion-notify-event",
                   G_CALLBACK(+[](GtkWidget *, GdkEventMotion *event, gpointer data) -> gboolean {
                     auto &self = *static_cast<Window *>(data);
                     if (self.pointer_known_) {
                       self.mouse_x_ += static_cast<int>(event->x - self.pointer_x_);
                       self.mouse_y_ += static_cast<int>(event->y - self.pointer_y_);
                     }
                     self.pointer_x_ = event->x;
                     self.pointer_y_ = event->y;
                     self.pointer_known_ = true;
                     return TRUE;
                   }),
                   this);
  g_signal_connect(drawing_, "leave-notify-event",
                   G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer data) -> gboolean {
                     static_cast<Window *>(data)->pointer_known_ = false;
                     return FALSE;
                   }),
                   this);
  const auto mouse = +[](GtkWidget *, GdkEventButton *event, gpointer data) -> gboolean {
    auto &self = *static_cast<Window *>(data);
    if (event->button == 1)
      self.mouse_left_ = event->type == GDK_BUTTON_PRESS;
    if (event->button == 3)
      self.mouse_right_ = event->type == GDK_BUTTON_PRESS;
    return TRUE;
  };
  g_signal_connect(drawing_, "button-press-event", G_CALLBACK(mouse), this);
  g_signal_connect(drawing_, "button-release-event", G_CALLBACK(mouse), this);
  gtk_widget_show_all(window_);
  try {
    if (settings_.gdb_enabled && !options_.frames)
      gdb_->listen(settings_.gdb_port);
    if (!options_.rom.empty() || !options_.ipl.empty())
      load(options_);
  } catch (const std::exception &failure) {
    if (options_.frames)
      throw;
    error(failure);
  }
  timer_ = g_timeout_add(1, timer, this);
  gtk_main();
  finish_playback();
  if (options_.frames && !terminal_frames_ && result_ == 0)
    result_ = 2;
  return result_;
}

GtkWidget *Window::item(GtkWidget *menu, const char *text, Command action) {
  const auto widget = action == Command::Pause ? gtk_check_menu_item_new_with_label(text)
                                               : gtk_menu_item_new_with_label(text);
  g_object_set_data(G_OBJECT(widget), "command", GINT_TO_POINTER(static_cast<int>(action)));
  g_signal_connect(widget, "activate", G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                     auto &self = *static_cast<Window *>(data);
                     try {
                       self.command(static_cast<Command>(
                           GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "command"))));
                     } catch (const std::exception &failure) {
                       self.error(failure);
                     }
                   }),
                   this);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), widget);
  return widget;
}

void Window::create_menu(GtkWidget *box) {
  const auto bar = gtk_menu_bar_new();
  gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
  const auto menu = [&](const char *name) {
    const auto top = gtk_menu_item_new_with_label(name), submenu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(top), submenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), top);
    return submenu;
  };
  const auto file = menu("File");
  item(file, "Open game…", Command::Open);
  item(file, "Select PIF firmware…", Command::Firmware);
  item(file, "Insert disk…", Command::Disk);
  item(file, "Eject disk", Command::Eject);
  item(file, "Close game", Command::Close);
  item(file, "Quit", Command::Quit);
  const auto emulation = menu("Emulation");
  pause_item_ = item(emulation, "Pause (F8)", Command::Pause);
  item(emulation, "Advance frame (F9)", Command::Advance);
  item(emulation, "Reset", Command::Reset);
  item(emulation, "Settings…", Command::Settings);
  item(emulation, "Keyboard controls", Command::Controls);
  const auto state = menu("State");
  item(state, "Save state (F5)", Command::SaveState);
  item(state, "Load state (F7)", Command::LoadState);
  item(state, "Undo save", Command::UndoSave);
  item(state, "Undo load", Command::UndoLoad);
  for (unsigned n = 1; n <= StateSlots::count; ++n) {
    const auto label = "Slot " + std::to_string(n);
    const auto slot = gtk_menu_item_new_with_label(label.c_str());
    g_object_set_data(G_OBJECT(slot), "slot", GUINT_TO_POINTER(n));
    g_signal_connect(slot, "activate", G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
                       auto &self = *static_cast<Window *>(data);
                       try {
                         self.finish_playback();
                         if (self.states_)
                           self.states_->select(
                               GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "slot")));
                         self.refresh();
                       } catch (const std::exception &failure) {
                         self.error(failure);
                       }
                     }),
                     this);
    gtk_menu_shell_append(GTK_MENU_SHELL(state), slot);
  }
  const auto view = menu("View");
  item(view, "Fullscreen (F11)", Command::Fullscreen);
  item(view, "Save screenshot…", Command::Screenshot);
}

std::filesystem::path Window::choose_file(const char *title, bool folder, bool saving) {
  finish_playback();
  const bool prior_modal = modal_;
  modal_ = true;
  audio_.clear();
  input_.clear();
  const auto dialog = gtk_file_chooser_dialog_new(
      title, GTK_WINDOW(window_),
      folder   ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
      : saving ? GTK_FILE_CHOOSER_ACTION_SAVE
               : GTK_FILE_CHOOSER_ACTION_OPEN,
      "Cancel", GTK_RESPONSE_CANCEL, saving ? "Save" : "Select", GTK_RESPONSE_ACCEPT, nullptr);
  if (saving)
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
  if (saving) {
    const auto image = options_.rom.empty() ? options_.ipl : options_.rom;
    const auto folder = settings_.session.storage.screenshots.empty()
                            ? image.parent_path()
                            : settings_.session.storage.screenshots;
    if (!folder.empty())
      gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), folder.c_str());
    const auto name = image.stem().native() + ".bmp";
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), name.c_str());
  }
  std::filesystem::path result;
  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
    if (auto name = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog))) {
      result = name;
      g_free(name);
    }
  }
  gtk_widget_destroy(dialog);
  modal_ = prior_modal;
  rebase();
  return result;
}

void Window::load(LaunchFiles files) {
  finish_playback();
  if (files.firmware.empty())
    files.firmware = files.arcade_profile == n64::ArcadeProfile::Disabled
                         ? settings_.firmware.n64
                         : settings_.firmware.arcade;
  validate_launch(files);
  if (session_)
    session_->save();
  auto next = std::make_unique<Session>(files.rom, files.firmware, audio_, files.ipl, files.disk,
                                        files.arcade_profile, std::nullopt, settings_.hardware,
                                        settings_.session);
  gdb_->target(nullptr);
  poll_gdb();
  states_.reset();
  session_ = std::move(next);
  static_cast<LaunchFiles &>(options_) = std::move(files);
  states_ = std::make_unique<StateSlots>(
      session_->storage_image(), [this] { return session_->capture_state(); },
      [this](std::span<const std::uint8_t> bytes) { session_->restore_state(bytes); });
  gdb_->target(&session_->debugger(), [this] { session_->debug_memory_fence(); });
  paused_ = false;
  failed_ = false;
  feedback_.clear();
  audio_.clear();
  input_.clear();
  pixels_.clear();
  last_frame_ = std::chrono::steady_clock::now();
  rebase();
  const auto image = options_.rom.empty() ? options_.ipl : options_.rom;
  const auto title = "Cupid-N64 | " + image.filename().native();
  gtk_window_set_title(GTK_WINDOW(window_), title.c_str());
  refresh();
}

void Window::close_game() {
  finish_playback();
  if (session_)
    session_->save();
  gdb_->target(nullptr);
  states_.reset();
  session_.reset();
  observed_frames_ = 0;
  hardware_rendering_ = debugger_attached_ = debugger_halted_ = false;
  controllers_ = {};
  feedback_state_ = {};
  input_.clear();
  audio_.clear();
  pixels_.clear();
  refresh();
}

void Window::close() {
  close_game();
  if (!fullscreen_ && window_) {
    int width = 0, height = 0;
    gtk_window_get_size(GTK_WINDOW(window_), &width, &height);
    settings_.width = static_cast<unsigned>(std::clamp(width, 320, 8192));
    settings_.height = static_cast<unsigned>(std::clamp(height, 240, 8192));
  }
  persist();
  gtk_widget_destroy(window_);
}

void Window::persist() {
  if (!options_.frames && !settings_path_.empty())
    save_settings(settings_path_, settings_);
}

void Window::rebase() {
  finish_playback();
  last_frame_ = std::chrono::steady_clock::now();
  if (session_)
    clock_.reset(session_->clocks(), PlaybackClock::Clock::now());
}

void Window::clear_input() {
  finish_playback();
  input_.clear();
  pointer_known_ = false;
  mouse_left_ = mouse_right_ = false;
  mouse_x_ = mouse_y_ = 0;
  {
    std::lock_guard lock(input_mutex_);
    worker_inputs_ = {};
    for (auto &mouse : worker_mouse_)
      mouse.clear();
  }
  if (session_) {
    session_->clear_mouse_input();
    std::array<FeedbackState, 4> state{};
    for (unsigned port = 0; port < state.size(); ++port)
      state[port] = session_->feedback_state(port);
    feedback_.reset(state);
  }
}

void Window::command(Command action) {
  finish_playback();
  if (action == Command::Quit) {
    close();
    return;
  }
  if (action == Command::Close) {
    close_game();
    return;
  }
  if (action == Command::Settings) {
    settings_dialog();
    return;
  }
  if (action == Command::Open) {
    const auto game = choose_file("Open Nintendo 64 game");
    if (game.empty())
      return;
    LaunchFiles files;
    files.rom = game;
    if (files.firmware.empty() && settings_.firmware.n64.empty())
      files.firmware = choose_file("Select Nintendo 64 PIF firmware");
    if (files.firmware.empty() && settings_.firmware.n64.empty())
      return;
    load(std::move(files));
    return;
  }
  if (action == Command::Firmware) {
    const auto selected = choose_file("Select Nintendo 64 PIF firmware");
    if (!selected.empty()) {
      validate_firmware(selected, FirmwareType::Nintendo64);
      settings_.firmware.n64 = selected;
      options_.firmware = selected;
      persist();
    }
    return;
  }
  if (action == Command::Controls) {
    const auto dialog = gtk_message_dialog_new(
        GTK_WINDOW(window_), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s",
        "Keyboard: J = A, K = B, Space = Z, Enter = Start.\n"
        "W/A/S/D = stick; arrow keys = C buttons; Q/E = L/R.\n"
        "I/O/U/P = D-pad; Shift = walk. Select gamepads and remap controls in Settings.");
    modal_ = true;
    audio_.clear();
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    modal_ = false;
    input_.clear();
    rebase();
    return;
  }
  if (action == Command::Fullscreen) {
    fullscreen_ = !fullscreen_;
    if (fullscreen_)
      gtk_window_fullscreen(GTK_WINDOW(window_));
    else
      gtk_window_unfullscreen(GTK_WINDOW(window_));
    return;
  }
  if (!session_)
    return;
  if (action == Command::SaveState || action == Command::LoadState || action == Command::UndoSave ||
      action == Command::UndoLoad) {
    bool restored = false;
    if (action == Command::SaveState)
      states_->save();
    if (action == Command::LoadState && states_->occupied(states_->selected())) {
      states_->load();
      restored = true;
    }
    if (action == Command::UndoSave && states_->undo_save_slot())
      states_->undo_save();
    if (action == Command::UndoLoad && states_->can_undo_load()) {
      states_->undo_load();
      restored = true;
    }
    if (restored) {
      clear_input();
      audio_.suspended = false;
      if (paused_)
        session_->finish_frame();
    }
    rebase();
    refresh();
    return;
  }
  if (action == Command::Pause && !session_->debugger().attached())
    paused_ = !paused_;
  if (action == Command::Advance && paused_ && !session_->debugger().attached())
    session_->advance_frame(inputs());
  if (action == Command::Reset) {
    session_->reset();
    failed_ = false;
  }
  if (action == Command::Disk) {
    const auto selected = choose_file("Insert 64DD disk");
    if (!selected.empty()) {
      session_->change_disk(selected);
      options_.disk = selected;
    }
  }
  if (action == Command::Eject) {
    session_->eject_disk();
    options_.disk.clear();
  }
  if (action == Command::Screenshot) {
    const auto path = choose_file("Save screenshot as BMP", false, true);
    if (!path.empty())
      screenshot(path);
  }
  session_->finish_frame();
  audio_.clear();
  clear_input();
  rebase();
  last_frame_ = std::chrono::steady_clock::now();
  refresh();
}

bool Window::stopped() const {
  if (session_ && !advancing_ && session_->debugger().attached())
    return session_->debugger().halted();
  if (session_ && debugger_attached_)
    return debugger_halted_;
  return paused_ || (!options_.frames && !active_ && settings_.focus == FocusBehavior::Pause);
}

PlayerInputs Window::inputs(bool scripted) {
  PlayerInputs result{};
  if (!options_.frames && !active_ && settings_.focus != FocusBehavior::AllowInput)
    return result;
  host_ = input_.poll();
  for (unsigned port = 0; port < result.size(); ++port) {
    result[port] = map_input(settings_.mappings[port], host_);
    if (controllers_[port] == ControllerType::Mouse) {
      result[port].mouse_x = mouse_x_;
      result[port].mouse_y = mouse_y_;
      result[port].mouse_left = mouse_left_;
      result[port].mouse_right = mouse_right_;
    }
  }
  mouse_x_ = mouse_y_ = 0;
  if (scripted && options_.test_input && observed_frames_ >= 120 && observed_frames_ % 120 < 60)
    result[0].buttons |= 0x1000;
  return result;
}

void Window::poll_gdb() {
  finish_playback();
  bool error = false;
  try {
    gdb_->poll();
  } catch (const std::exception &failure) {
    gdb_error_ = failure.what();
    error = true;
  }
  const bool connected = gdb_->connected();
  if (connected == gdb_connected_) {
    if (error)
      refresh();
    return;
  }
  if (connected) {
    gdb_error_.clear();
    gdb_was_paused_ = paused_;
  } else
    paused_ = gdb_was_paused_;
  gdb_connected_ = connected;
  audio_.clear();
  input_.clear();
  rebase();
  refresh();
}

gboolean Window::timer(gpointer data) {
  auto &self = *static_cast<Window *>(data);
  try {
    if (self.tick())
      return G_SOURCE_CONTINUE;
  } catch (const std::exception &failure) {
    try {
      self.finish_playback();
    } catch (...) {
    }
    self.paused_ = true;
    self.failed_ = true;
    if (self.session_)
      self.session_->debugger().interrupt();
    self.audio_.clear();
    if (self.options_.frames) {
      std::cerr << failure.what() << '\n';
      self.result_ = 1;
      gtk_widget_destroy(self.window_);
    } else {
      self.error(failure);
      self.refresh();
      return G_SOURCE_CONTINUE;
    }
  }
  self.timer_ = 0;
  return G_SOURCE_REMOVE;
}

bool Window::synchronous_tick() {
  if (!window_)
    return false;
  if (modal_)
    return true;
  poll_gdb();
  const auto now = PlaybackClock::Clock::now();
  if (now >= next_audio_probe_) {
    if (audio_.recover()) {
      rebase();
      refresh();
    }
    next_audio_probe_ = now + std::chrono::milliseconds(500);
  }
  if (stopped() != last_stopped_) {
    last_stopped_ = stopped();
    clear_input();
    audio_.clear();
    rebase();
    refresh();
  }
  if (!session_ || stopped() || failed_) {
    input_.stop();
    last_frame_ = std::chrono::steady_clock::now();
    return true;
  }
  if ((settings_.sync == PlaybackSync::Video || !audio_.available()) &&
      clock_.delay(session_->clocks(), now).count())
    return true;
  const auto deadline = now + std::chrono::milliseconds(4);
  const auto before = session_->frames;
  session_->input(inputs());
  do {
    session_->run();
    if (session_->debugger().halted())
      break;
    const auto control = session_->diagnostics().take_control();
    if (control == n64::HostControl::Exit) {
      close();
      return false;
    }
    if (session_->frames != before)
      break;
  } while (PlaybackClock::Clock::now() < deadline);
  if (session_->frames != before) {
    session_->finish_frame();
    last_frame_ = PlaybackClock::Clock::now();
    refresh();
    if (options_.frames && session_->frames >= options_.frames) {
      if (!options_.capture.empty())
        screenshot(options_.capture);
      terminal_frames_ = true;
      close();
      return false;
    }
  } else if (PlaybackClock::Clock::now() - last_frame_ > std::chrono::seconds(30))
    throw std::runtime_error("Emulation stopped because no video frame was produced.");
  std::array<FeedbackState, 4> state{};
  for (unsigned port = 0; port < state.size(); ++port)
    state[port] = session_->feedback_state(port);
  input_.rumble(
      feedback_targets(settings_.mappings, feedback_.update(state), host_, settings_.feedback));
  return true;
}

void Window::refresh(bool current_frame) {
  if (!window_ || !drawing_ || !status_)
    return;
  std::string status = "Open a game and select its PIF firmware to begin.";
  if (session_) {
    if (current_frame && !advancing_) {
      cache_session();
      refresh_frame(session_->frame);
    }
    status = hardware_rendering_ ? "Hardware rendering" : "Software rendering";
    status += stopped() ? " | Paused" : " | Running";
    status += " | Slot " + std::to_string(states_->selected());
    status += audio_.available() ? " | Audio: " + utf8(audio_.device_name())
                                 : " | Audio unavailable: " + utf8(audio_.error());
    if (gdb_->connected())
      status += " | GDB connected";
    if (!gdb_error_.empty())
      status += " | GDB: " + gdb_error_;
  }
  if (pause_item_) {
    g_signal_handlers_block_matched(pause_item_, G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, this);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(pause_item_), paused_);
    g_signal_handlers_unblock_matched(pause_item_, G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr,
                                      this);
  }
  gtk_label_set_text(GTK_LABEL(status_), status.c_str());
  gtk_widget_queue_draw(drawing_);
}

void Window::draw(cairo_t *context) {
  cairo_set_source_rgb(context, 0, 0, 0);
  cairo_paint(context);
  if (pixels_.empty())
    return;
  const auto area_width = static_cast<unsigned>(gtk_widget_get_allocated_width(drawing_));
  const auto area_height = static_cast<unsigned>(gtk_widget_get_allocated_height(drawing_));
  const auto viewport = image_viewport(frame_width_, frame_height_, area_width, area_height,
                                       settings_.scale, settings_.aspect);
  if (!viewport.width || !viewport.height)
    return;
  const auto surface = cairo_image_surface_create_for_data(
      pixels_.data(), CAIRO_FORMAT_RGB24, static_cast<int>(frame_width_),
      static_cast<int>(frame_height_), static_cast<int>(frame_width_ * 4));
  cairo_save(context);
  cairo_translate(context, viewport.x, viewport.y);
  cairo_scale(context, double(viewport.width) / frame_width_,
              double(viewport.height) / frame_height_);
  cairo_set_source_surface(context, surface, 0, 0);
  cairo_pattern_set_filter(cairo_get_source(context), CAIRO_FILTER_NEAREST);
  cairo_paint(context);
  cairo_restore(context);
  cairo_surface_destroy(surface);
}

void Window::screenshot(const std::filesystem::path &path) {
  if (pixels_.empty())
    throw std::runtime_error("No video frame is available to capture.");
  write_save(path, bitmap_image(frame_width_, frame_height_, pixels_));
}

void Window::error(const std::exception &failure) {
  finish_playback();
  const bool prior_modal = modal_;
  modal_ = true;
  const auto dialog =
      gtk_message_dialog_new(GTK_WINDOW(window_), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                             GTK_BUTTONS_CLOSE, "%s", failure.what());
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
  modal_ = prior_modal;
  rebase();
}

} // namespace cupid::desktop::linux_host
