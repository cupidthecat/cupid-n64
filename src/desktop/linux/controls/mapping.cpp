#include "desktop/linux/text.hpp"
#include "desktop/linux/window.hpp"
#include <algorithm>
#include <bit>

namespace cupid::desktop::linux_host {
namespace {

std::string label(InputBinding binding) {
  if (binding.kind == BindingKind::None)
    return "None";
  if (binding.kind == BindingKind::Key) {
    if (binding.code >= 0x70 && binding.code <= 0x87)
      return "F" + std::to_string(binding.code - 0x70 + 1);
    switch (binding.code) {
    case 0x0d:
      return "Enter";
    case 0x10:
      return "Shift";
    case 0x11:
      return "Control";
    case 0x12:
      return "Alt";
    case 0x20:
      return "Space";
    case 0x25:
      return "Left";
    case 0x26:
      return "Up";
    case 0x27:
      return "Right";
    case 0x28:
      return "Down";
    case 0xba:
      return ";";
    case 0xbb:
      return "=";
    case 0xbc:
      return ",";
    case 0xbd:
      return "-";
    case 0xbe:
      return ".";
    case 0xbf:
      return "/";
    case 0xc0:
      return "`";
    case 0xdb:
      return "[";
    case 0xdc:
      return "\\";
    case 0xdd:
      return "]";
    case 0xde:
      return "'";
    default:
      return utf8(std::wstring(1, static_cast<wchar_t>(binding.code)));
    }
  }
  if (binding.kind == BindingKind::GamepadButton) {
    constexpr std::array<const char *, 16> names{
        "D-pad Up",         "D-pad Down",        "D-pad Left",    "D-pad Right",    "Start", "Back",
        "Left stick click", "Right stick click", "Left shoulder", "Right shoulder", "",      "",
        "South button",     "East button",       "West button",   "North button"};
    return names.at(std::countr_zero(binding.code));
  }
  constexpr std::array<const char *, 6> axes{"Left X",  "Left Y",       "Right X",
                                             "Right Y", "Left trigger", "Right trigger"};
  return std::string(axes.at(binding.code)) +
         (binding.kind == BindingKind::AxisNegative ? " −" : " +");
}

struct Capture {
  Input *input;
  ControllerMapping *mapping;
  InputBinding *pending = nullptr;
  GtkWidget *button = nullptr;
  HostGamepad previous{};
  void finish(InputBinding binding) {
    *pending = binding;
    gtk_button_set_label(GTK_BUTTON(button), label(binding).c_str());
    pending = nullptr;
    button = nullptr;
  }
};

} // namespace

void Window::mapping_dialog(unsigned port, ControllerMapping &mapping) {
  auto next = mapping;
  const auto title = "Port " + std::to_string(port + 1) + " bindings";
  const auto dialog =
      gtk_dialog_new_with_buttons(title.c_str(), GTK_WINDOW(window_), GTK_DIALOG_MODAL, "Cancel",
                                  GTK_RESPONSE_CANCEL, "Apply", GTK_RESPONSE_APPLY, nullptr);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 640, 600);
  const auto box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  const auto help =
      gtk_label_new("Click a binding, then press a key or gamepad control. Escape clears it.");
  gtk_box_pack_start(GTK_BOX(box), help, FALSE, FALSE, 8);
  const auto scroll = gtk_scrolled_window_new(nullptr, nullptr), grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
  gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
  gtk_container_add(GTK_CONTAINER(scroll), grid);
  gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
  Capture capture{&input_, &next};
  const auto button = [&](InputBinding &binding) {
    const auto widget = gtk_button_new_with_label(label(binding).c_str());
    g_object_set_data(G_OBJECT(widget), "binding", &binding);
    g_signal_connect(
        widget, "clicked", G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
          auto &capture = *static_cast<Capture *>(data);
          if (capture.pending)
            gtk_button_set_label(GTK_BUTTON(capture.button), label(*capture.pending).c_str());
          capture.pending =
              static_cast<InputBinding *>(g_object_get_data(G_OBJECT(widget), "binding"));
          capture.button = widget;
          const auto host = capture.input->poll();
          if (capture.mapping->source >= HostSource::Gamepad1)
            capture.previous = host.gamepads[static_cast<unsigned>(capture.mapping->source) -
                                             static_cast<unsigned>(HostSource::Gamepad1)];
          gtk_button_set_label(GTK_BUTTON(widget), "Press a control…");
        }),
        &capture);
    return widget;
  };
  for (unsigned n = 0; n < next.buttons.size(); ++n) {
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(utf8(button_names[n]).c_str()), 0,
                    static_cast<int>(n), 1, 1);
    gtk_grid_attach(GTK_GRID(grid), button(next.buttons[n]), 1, static_cast<int>(n), 3, 1);
  }
  std::array<GtkWidget *, 6> sources{}, inverted{};
  for (unsigned n = 0; n < next.axes.size(); ++n) {
    const auto row = static_cast<int>(next.buttons.size() + n * 2);
    auto &axis = next.axes[n];
    sources[n] = gtk_combo_box_text_new();
    for (const auto name : {"Buttons only", "Left stick X", "Left stick Y", "Right stick X",
                            "Right stick Y", "Left trigger", "Right trigger"})
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sources[n]), name);
    gtk_combo_box_set_active(GTK_COMBO_BOX(sources[n]), static_cast<int>(axis.source));
    inverted[n] = gtk_check_button_new_with_label("Invert");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(inverted[n]), axis.inverted);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(utf8(axis_names[n]).c_str()), 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sources[n], 1, row, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), inverted[n], 3, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), button(axis.negative), 1, row + 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), button(axis.positive), 2, row + 1, 1, 1);
  }
  const auto slow_row = static_cast<int>(next.buttons.size() + next.axes.size() * 2);
  gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Walk"), 0, slow_row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), button(next.slow), 1, slow_row, 3, 1);
  g_signal_connect(dialog, "key-press-event",
                   G_CALLBACK(+[](GtkWidget *, GdkEventKey *event, gpointer data) -> gboolean {
                     auto &capture = *static_cast<Capture *>(data);
                     if (!capture.pending)
                       return FALSE;
                     if (event->keyval == GDK_KEY_Escape)
                       capture.finish({});
                     else if (capture.mapping->source == HostSource::Keyboard) {
                       if (const auto code = key_code(event->keyval))
                         capture.finish({BindingKind::Key, code});
                     }
                     return TRUE;
                   }),
                   &capture);
  const auto poll = g_timeout_add(
      25,
      +[](gpointer data) -> gboolean {
        auto &capture = *static_cast<Capture *>(data);
        if (!capture.pending || capture.mapping->source < HostSource::Gamepad1)
          return G_SOURCE_CONTINUE;
        const auto host = capture.input->poll();
        const auto &pad = host.gamepads[static_cast<unsigned>(capture.mapping->source) -
                                        static_cast<unsigned>(HostSource::Gamepad1)];
        if (!pad.connected)
          return G_SOURCE_CONTINUE;
        const unsigned buttons = pad.buttons & ~capture.previous.buttons;
        if (buttons) {
          capture.finish({BindingKind::GamepadButton, 1u << std::countr_zero(buttons)});
          return G_SOURCE_CONTINUE;
        }
        for (unsigned axis = 0; axis < pad.axes.size(); ++axis) {
          if (pad.axes[axis] < -20000 && capture.previous.axes[axis] >= -20000) {
            capture.finish({BindingKind::AxisNegative, axis});
            break;
          }
          if (pad.axes[axis] > 20000 && capture.previous.axes[axis] <= 20000) {
            capture.finish({BindingKind::AxisPositive, axis});
            break;
          }
        }
        capture.previous = pad;
        return G_SOURCE_CONTINUE;
      },
      &capture);
  gtk_widget_show_all(dialog);
  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_APPLY) {
    for (unsigned n = 0; n < next.axes.size(); ++n) {
      next.axes[n].source =
          static_cast<HostAxis>(gtk_combo_box_get_active(GTK_COMBO_BOX(sources[n])));
      next.axes[n].inverted = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(inverted[n]));
    }
    mapping = next;
  }
  g_source_remove(poll);
  gtk_widget_destroy(dialog);
  input_.clear();
}

} // namespace cupid::desktop::linux_host
