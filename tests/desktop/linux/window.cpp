#include "desktop/linux/window.hpp"
#include "../../support/test.hpp"
#include "../state/fixture.hpp"
#include "host.hpp"
#include <algorithm>

namespace {

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
  g_timeout_add(
      50,
      +[](gpointer data) -> gboolean {
        auto &observation = *static_cast<Observation *>(data);
        observation.state_restored = true;
        const auto window = top("Cupid-N64");
        test::equal(window != nullptr, true);
        if (!window)
          return G_SOURCE_REMOVE;
        key(window, GDK_KEY_F5);
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
  test::equal(window.run(), 0);
  test::equal(observation.ran, true);
  test::equal(observation.state_restored, true);
  const auto bitmap = read_file(options.capture, 1024 * 1024);
  test::equal(bitmap.size() > 54 && bitmap[0] == 'B' && bitmap[1] == 'M', true);
  test::equal(std::any_of(bitmap.begin() + 54, bitmap.end(), [](auto byte) { return byte != 0; }),
              true);
  test::equal(std::filesystem::is_regular_file(directory.path / "game.z64.states" / "slot-1.state"),
              true);
  test::equal(read_file(config, 1024 * 1024) == original_settings, true);
}
