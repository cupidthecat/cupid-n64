#include "desktop/linux/window.hpp"
#include "../../support/test.hpp"
#include "../state/fixture.hpp"
#include "host.hpp"
#include <algorithm>
#include <thread>

namespace cupid::desktop::linux_host {
struct WindowTestAccess {
  static void attach(Window &window, const std::filesystem::path &directory) {
    window.session_ = std::make_unique<Session>(
        directory / "game.z64", directory / "pif.bin", window.audio_, std::filesystem::path{},
        std::filesystem::path{}, n64::ArcadeProfile::Disabled, 0, false);
    window.cache_session();
    window.rebase();
  }
  static void start(Window &window) {
    window.start_playback();
  }
  static void drain(Window &window) {
    window.drain_playback();
  }
  static void finish(Window &window) {
    window.finish_playback();
  }
  static bool running(const Window &window) {
    return window.advancing_.load();
  }
  static bool ready(const Window &window) {
    return window.execution_.ready();
  }
  static unsigned frames(const Window &window) {
    return window.observed_frames_;
  }
  static unsigned guest_frames(const Window &window) {
    return window.session_->frames;
  }
  static std::uint64_t refreshes(const Window &window) {
    return window.refresh_count_.load();
  }
  static const std::vector<std::uint8_t> &pixels(const Window &window) {
    return window.pixels_;
  }
  static bool queued(Window &window) {
    return bool(window.completed_fields_->take());
  }
  static void mouse(Window &window, std::int32_t x, std::int32_t y) {
    window.controllers_[0] = ControllerType::Mouse;
    window.mouse_left_ = true;
    window.mouse_x_ = x;
    window.mouse_y_ = y;
    window.publish_inputs();
  }
  static PlayerInputs input(Window &window) {
    return window.take_inputs();
  }
};
} // namespace cupid::desktop::linux_host

namespace {

void changing_video(const std::filesystem::path &directory, bool inactive = false) {
  using namespace cupid::desktop;
  session_test::media(directory, 32);
  auto pif = read_file(directory / "pif.bin", 0x7c0);
  std::vector<std::uint32_t> code;
  const auto word = [&](unsigned n) {
    return (std::uint32_t(pif[n]) << 24) | (std::uint32_t(pif[n + 1]) << 16) |
           (std::uint32_t(pif[n + 2]) << 8) | pif[n + 3];
  };
  bool inserted = false;
  for (unsigned n = 0; n < pif.size(); n += 4) {
    if (!inserted && word(n) == 0x3c017fff) {
      inserted = true;
      // Each guest loop changes the first pixel before the next scanout.
      const std::array<std::uint32_t, 8> loop{0x3c03a000, 0x34631000, 0x3c04f800, 0x3c050001,
                                              0x00852021, 0xac640000, 0x1000fffd, 0};
      for (unsigned instruction = 0; instruction < loop.size(); ++instruction) {
        const auto address = 0xa0002000 + instruction * 4;
        code.insert(code.end(), {0x3c010000 | (address >> 16), 0x34210000 | (address & 0xffff),
                                 0x3c020000 | (loop[instruction] >> 16),
                                 0x34420000 | (loop[instruction] & 0xffff), 0xac220000});
      }
    }
    auto instruction = word(n);
    if (n >= 12 && word(n - 12) == 0x3c01a440 && word(n - 8) == 0x34210018 &&
        instruction == 0x34420003)
      instruction = 0x34420026;
    if (n >= 12 && word(n - 12) == 0x3c01a440 && word(n - 8) == 0x34210028 &&
        instruction == 0x34420002) {
      code.back() = 0x3c020022;
      instruction = 0x34420024;
    }
    if (n >= 12 && word(n - 12) == 0x3c01a440 && word(n - 8) == 0x34210000 &&
        instruction == 0x34420002)
      instruction = inactive ? 0x34420000 : 0x34420042;
    code.push_back(instruction);
    if (inserted && instruction == 0x00200008) {
      code.push_back(0);
      break;
    }
  }
  test::equal(inserted, true);
  std::fill(pif.begin(), pif.end(), 0);
  for (unsigned n = 0; n < code.size(); ++n)
    for (unsigned b = 0; b < 4; ++b)
      pif.at(n * 4 + b) = static_cast<std::uint8_t>(code[n] >> (24 - b * 8));
  write_save(directory / "pif.bin", pif);
}

void frame_observer(const std::filesystem::path &directory) {
  using namespace cupid::desktop;
  changing_video(directory);
  Audio first_audio, second_audio;
  Session ordinary(directory / "game.z64", directory / "pif.bin", first_audio, {}, {},
                   cupid::n64::ArcadeProfile::Disabled, 0, false);
  Session observed(directory / "game.z64", directory / "pif.bin", second_audio, {}, {},
                   cupid::n64::ArcadeProfile::Disabled, 0, false);
  unsigned callbacks = 0;
  std::uint64_t prior_clock = 0;
  std::vector<std::uint8_t> first_image;
  observed.observe_frames([&](unsigned frame) {
    test::equal(frame, ++callbacks);
    test::equal(observed.frames, frame);
    test::equal(observed.clocks() >= prior_clock, true);
    prior_clock = observed.clocks();
    observed.finish_frame();
    if (first_image.empty())
      first_image = observed.frame.rgba;
  });
  bool multiple = false;
  for (unsigned interval = 0; interval < 10000 && (observed.frames < 128 || !multiple);
       ++interval) {
    const auto before = observed.frames;
    ordinary.run();
    observed.run();
    multiple |= observed.frames - before > 1;
    test::equal(observed.clocks(), ordinary.clocks());
    test::equal(observed.frames, ordinary.frames);
  }
  observed.observe_frames({});
  ordinary.finish_frame();
  observed.finish_frame();
  test::equal(multiple, true);
  test::equal(callbacks, ordinary.frames);
  test::equal(first_image.empty(), false);
  test::equal(first_image != observed.frame.rgba, true);
  test::equal(observed.capture_state() == ordinary.capture_state(), true);
}

void playback_ownership(const std::filesystem::path &directory) {
  using namespace cupid::desktop;
  using namespace cupid::desktop::linux_host;
  {
    Window mouse({}, {}, false);
    WindowTestAccess::mouse(mouse, 10, -20);
    WindowTestAccess::mouse(mouse, 30, -40);
    const auto moved = WindowTestAccess::input(mouse)[0];
    test::equal(moved.mouse_x, 40);
    test::equal(moved.mouse_y, -60);
    test::equal(moved.mouse_left, true);
    const auto held = WindowTestAccess::input(mouse)[0];
    test::equal(held.mouse_x, 0);
    test::equal(held.mouse_y, 0);
    test::equal(held.mouse_left, true);
  }
  changing_video(directory);
  Audio audio;
  Session expected(directory / "game.z64", directory / "pif.bin", audio, {}, {},
                   cupid::n64::ArcadeProfile::Disabled, 0, false);
  unsigned target_field = 0;
  std::vector<std::uint8_t> target;
  expected.observe_frames([&](unsigned frame) {
    expected.finish_frame();
    if (frame == target_field)
      target = expected.frame.rgba;
  });
  for (unsigned interval = 0; interval < 10000; ++interval) {
    if (expected.frames >= 120)
      target_field = expected.frames + 1;
    expected.run();
    if (target_field && expected.frames > target_field && expected.frame.rgba != target)
      break;
  }
  test::equal(target_field != 0 && expected.frames > target_field, true);
  test::equal(expected.frame.rgba != target, true);
  Options options;
  options.frames = target_field;
  Window window(options, {}, false);
  WindowTestAccess::attach(window, directory);
  WindowTestAccess::start(window);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!WindowTestAccess::ready(window) && std::chrono::steady_clock::now() < deadline) {
    WindowTestAccess::drain(window);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  test::equal(WindowTestAccess::ready(window), true);
  WindowTestAccess::finish(window);
  test::equal(WindowTestAccess::running(window), false);
  test::equal(WindowTestAccess::frames(window), target_field);
  test::equal(WindowTestAccess::guest_frames(window) > target_field, true);
  for (std::size_t n = 0; n < target.size(); n += 4)
    std::swap(target[n], target[n + 2]);
  test::equal(WindowTestAccess::pixels(window) == target, true);

  Window blocked({}, {}, false);
  WindowTestAccess::attach(blocked, directory);
  WindowTestAccess::start(blocked);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  test::equal(WindowTestAccess::running(blocked), true);
  test::equal(WindowTestAccess::ready(blocked), false);
  const auto joined = std::chrono::steady_clock::now();
  WindowTestAccess::finish(blocked);
  test::equal(std::chrono::steady_clock::now() - joined < std::chrono::seconds(1), true);
  test::equal(WindowTestAccess::frames(blocked), 3);
  test::equal(WindowTestAccess::queued(blocked), false);

  changing_video(directory, true);
  Window inactive({}, {}, false);
  WindowTestAccess::attach(inactive, directory);
  WindowTestAccess::start(inactive);
  const auto idle_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (WindowTestAccess::refreshes(inactive) < 2 &&
         std::chrono::steady_clock::now() < idle_deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  test::equal(WindowTestAccess::refreshes(inactive) >= 2, true);
  WindowTestAccess::finish(inactive);
  test::equal(WindowTestAccess::guest_frames(inactive), 0);
  test::equal(WindowTestAccess::frames(inactive), 0);
  test::equal(WindowTestAccess::pixels(inactive).empty(), true);
  test::equal(WindowTestAccess::queued(inactive), false);
}

GtkWidget *find(GtkWidget *widget, const std::function<bool(GtkWidget *)> &matches) {
  if (matches(widget))
    return widget;
  if (!GTK_IS_CONTAINER(widget))
    return nullptr;
  auto children = gtk_container_get_children(GTK_CONTAINER(widget));
  GtkWidget *result = nullptr;
  for (auto child = children; child && !result; child = child->next)
    result = find(GTK_WIDGET(child->data), matches);
  g_list_free(children);
  return result;
}

GtkWidget *top(const char *title) {
  const auto windows = gtk_window_list_toplevels();
  GtkWidget *result = nullptr;
  for (auto window = windows; window; window = window->next) {
    const auto widget = GTK_WIDGET(window->data);
    const auto name = gtk_window_get_title(GTK_WINDOW(widget));
    if (name && std::string_view(name).starts_with(title)) {
      result = widget;
      break;
    }
  }
  g_list_free(windows);
  return result;
}

void key(GtkWidget *window, unsigned keyval, bool release = true) {
  GdkEventKey event{};
  event.type = GDK_KEY_PRESS;
  event.keyval = keyval;
  event.hardware_keycode = static_cast<guint16>(cupid::desktop::linux_host::key_code(keyval));
  gboolean handled = FALSE;
  g_signal_emit_by_name(window, "key-press-event", &event, &handled);
  test::equal(handled, true);
  if (release) {
    event.type = GDK_KEY_RELEASE;
    g_signal_emit_by_name(window, "key-release-event", &event, &handled);
  }
}

bool status(GtkWidget *window, const char *text) {
  return find(window, [&](GtkWidget *widget) {
           return GTK_IS_LABEL(widget) &&
                  std::string_view(gtk_label_get_text(GTK_LABEL(widget))).find(text) !=
                      std::string_view::npos;
         }) != nullptr;
}

void resolution(GtkWidget *item, bool apply) {
  struct Visit {
    bool apply, visited = false;
    unsigned source = 0;
  } visit{apply};
  visit.source = g_timeout_add(
      1,
      +[](gpointer data) -> gboolean {
        auto &visit = *static_cast<Visit *>(data);
        const auto windows = gtk_window_list_toplevels();
        GtkWidget *error = nullptr;
        for (auto window = windows; window; window = window->next)
          if (GTK_IS_MESSAGE_DIALOG(window->data))
            error = GTK_WIDGET(window->data);
        g_list_free(windows);
        if (error) {
          test::equal(false, true);
          gtk_dialog_response(GTK_DIALOG(error), GTK_RESPONSE_CLOSE);
          return G_SOURCE_CONTINUE;
        }
        const auto dialog = top("Settings");
        if (!dialog) {
          visit.source = 0;
          return G_SOURCE_REMOVE;
        }
        if (visit.visited) {
          gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
          return G_SOURCE_CONTINUE;
        }
        visit.visited = true;
        const auto notebook =
            find(dialog, [](GtkWidget *widget) { return GTK_IS_NOTEBOOK(widget); });
        const auto page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), 0);
        const auto grid = find(page, [](GtkWidget *widget) { return GTK_IS_GRID(widget); });
        const auto scale = gtk_grid_get_child_at(GTK_GRID(grid), 1, 1);
        if (visit.apply) {
          gtk_combo_box_set_active(GTK_COMBO_BOX(scale), 1);
          gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY);
        } else {
          test::equal(gtk_combo_box_get_active(GTK_COMBO_BOX(scale)), 1);
          gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
        }
        return G_SOURCE_CONTINUE;
      },
      &visit);
  gtk_menu_item_activate(GTK_MENU_ITEM(item));
  if (visit.source)
    g_source_remove(visit.source);
  test::equal(visit.visited, true);
}

} // namespace

void linux_playback_tests() {
  session_test::Directory playback;
  frame_observer(playback.path);
  playback_ownership(playback.path);
}

void linux_window_tests(bool hardware) {
  using namespace cupid::desktop;
  using namespace cupid::desktop::linux_host;
  if (!gtk_init_check(nullptr, nullptr))
    throw std::runtime_error("The window test requires a display.");
  session_test::Directory directory;
  session_test::media(directory.path);
  const auto config = directory.path / "settings.ini";
  Settings settings;
  settings.hardware = hardware;
  settings.audio.levels.volume = 125;
  save_settings(config, settings);
  const auto original_settings = read_file(config, 1024 * 1024);
  struct Observation {
    bool hardware;
    std::filesystem::path directory;
    bool ran = false;
    bool state_restored = false;
    Window *owner = nullptr;
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
  } observation{hardware, directory.path};
  g_timeout_add(
      0,
      +[](gpointer data) -> gboolean {
        auto &observation = *static_cast<Observation *>(data);
        observation.ran = true;
        const auto window = top("Cupid-N64");
        test::equal(window != nullptr, true);
        if (!window)
          return G_SOURCE_REMOVE;
        test::equal(
            status(window, observation.hardware ? "Hardware rendering" : "Software rendering"),
            true);
        test::equal(status(window, "Audio: "), true);
        key(window, GDK_KEY_F8, false);
        key(window, GDK_KEY_F8);
        test::equal(status(window, "Paused"), true);
        key(window, GDK_KEY_F9);
        key(window, GDK_KEY_F5);
        key(window, GDK_KEY_F7);
        key(window, GDK_KEY_F8);
        test::equal(status(window, "Running"), true);
        const auto menu = find(window, [](GtkWidget *widget) {
          return GTK_IS_MENU_ITEM(widget) &&
                 std::string_view(gtk_menu_item_get_label(GTK_MENU_ITEM(widget))) == "Emulation";
        });
        test::equal(menu != nullptr, true);
        const auto settings_item =
            menu ? find(gtk_menu_item_get_submenu(GTK_MENU_ITEM(menu)),
                        [](GtkWidget *widget) {
                          return GTK_IS_MENU_ITEM(widget) &&
                                 std::string_view(gtk_menu_item_get_label(GTK_MENU_ITEM(widget))) ==
                                     "Settings…";
                        })
                 : nullptr;
        test::equal(settings_item != nullptr, true);
        if (settings_item) {
          g_timeout_add(
              0,
              +[](gpointer) -> gboolean {
                const auto dialog = top("Settings");
                test::equal(dialog != nullptr, true);
                if (dialog) {
                  const auto notebook =
                      find(dialog, [](GtkWidget *widget) { return GTK_IS_NOTEBOOK(widget); });
                  test::equal(notebook != nullptr, true);
                  if (notebook)
                    test::equal(gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)), 5);
                  gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
                }
                return G_SOURCE_REMOVE;
              },
              nullptr);
          gtk_menu_item_activate(GTK_MENU_ITEM(settings_item));
          if (observation.hardware) {
            resolution(settings_item, true);
            resolution(settings_item, false);
          }
        }
        return G_SOURCE_REMOVE;
      },
      &observation);
  const auto restore_source = g_timeout_add(
      50,
      +[](gpointer data) -> gboolean {
        auto &observation = *static_cast<Observation *>(data);
        if (!WindowTestAccess::running(*observation.owner) &&
            std::chrono::steady_clock::now() < observation.deadline)
          return G_SOURCE_CONTINUE;
        observation.state_restored = true;
        test::equal(WindowTestAccess::running(*observation.owner), true);
        const auto window = top("Cupid-N64");
        test::equal(window != nullptr, true);
        if (!window)
          return G_SOURCE_REMOVE;
        key(window, GDK_KEY_F5);
        test::equal(WindowTestAccess::running(*observation.owner), false);
        const auto path = observation.directory / "game.z64.states" / "slot-1.state";
        const auto saved = read_file(path, 256 * 1024 * 1024);
        const auto menu = find(window, [](GtkWidget *widget) {
          return GTK_IS_MENU_ITEM(widget) &&
                 std::string_view(gtk_menu_item_get_label(GTK_MENU_ITEM(widget))) == "Emulation";
        });
        const auto reset =
            menu ? find(gtk_menu_item_get_submenu(GTK_MENU_ITEM(menu)),
                        [](GtkWidget *widget) {
                          return GTK_IS_MENU_ITEM(widget) &&
                                 std::string_view(gtk_menu_item_get_label(GTK_MENU_ITEM(widget))) ==
                                     "Reset";
                        })
                 : nullptr;
        test::equal(reset != nullptr, true);
        if (reset)
          gtk_menu_item_activate(GTK_MENU_ITEM(reset));
        key(window, GDK_KEY_F7);
        key(window, GDK_KEY_F5);
        test::equal(read_file(path, 256 * 1024 * 1024) == saved, true);
        return G_SOURCE_REMOVE;
      },
      &observation);
  Options options;
  options.rom = directory.path / "game.z64";
  options.firmware = directory.path / "pif.bin";
  options.frames = 30;
  options.capture = directory.path / "frame.bmp";
  Window window(options, config);
  observation.owner = &window;
  test::equal(window.run(), 0);
  if (g_main_context_find_source_by_id(nullptr, restore_source))
    g_source_remove(restore_source);
  test::equal(observation.ran, true);
  test::equal(observation.state_restored, true);
  const auto bitmap = read_file(options.capture, 1024 * 1024);
  test::equal(bitmap.size() > 54 && bitmap[0] == 'B' && bitmap[1] == 'M', true);
  test::equal(std::any_of(bitmap.begin() + 54, bitmap.end(), [](auto byte) { return byte != 0; }),
              true);
  test::equal(std::filesystem::is_regular_file(directory.path / "game.z64.states" / "slot-1.state"),
              true);
  test::equal(read_file(config, 1024 * 1024) == original_settings, true);
  if (!hardware) {
    options.frames = 1000;
    options.capture.clear();
    Window early(options, config);
    struct Close {
      Window &owner;
      bool ran = false;
    } close{early};
    const auto closing = g_timeout_add(
        10,
        +[](gpointer data) -> gboolean {
          auto &close = *static_cast<Close *>(data);
          if (!WindowTestAccess::running(close.owner))
            return G_SOURCE_CONTINUE;
          close.ran = true;
          gtk_widget_destroy(top("Cupid-N64"));
          return G_SOURCE_REMOVE;
        },
        &close);
    test::equal(early.run(), 2);
    test::equal(close.ran, true);
    test::equal(WindowTestAccess::running(early), false);
    if (g_main_context_find_source_by_id(nullptr, closing))
      g_source_remove(closing);
  }
}
