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
    bool ran = false;
  } observation{hardware};
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
        }
        return G_SOURCE_REMOVE;
      },
      &observation);
  Options options;
  options.rom = directory.path / "game.z64";
  options.firmware = directory.path / "pif.bin";
  options.frames = 3;
  options.capture = directory.path / "frame.bmp";
  Window window(options, config);
  test::equal(window.run(), 0);
  test::equal(observation.ran, true);
  const auto bitmap = read_file(options.capture, 1024 * 1024);
  test::equal(bitmap.size() > 54 && bitmap[0] == 'B' && bitmap[1] == 'M', true);
  test::equal(std::any_of(bitmap.begin() + 54, bitmap.end(), [](auto byte) { return byte != 0; }),
              true);
  test::equal(std::filesystem::is_regular_file(directory.path / "game.z64.states" / "slot-1.state"),
              true);
  test::equal(read_file(config, 1024 * 1024) == original_settings, true);
}
