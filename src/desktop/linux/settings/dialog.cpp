#include "desktop/linux/audio/devices.hpp"
#include "desktop/linux/text.hpp"
#include "desktop/linux/window.hpp"
#include <algorithm>
#include <array>

namespace cupid::desktop::linux_host {
namespace {

GtkWidget *combo(std::initializer_list<const char *> names, unsigned selected) {
  const auto widget = gtk_combo_box_text_new();
  for (const auto name : names)
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widget), name);
  gtk_combo_box_set_active(GTK_COMBO_BOX(widget), static_cast<int>(selected));
  return widget;
}

GtkWidget *check(const char *text, bool selected) {
  const auto widget = gtk_check_button_new_with_label(text);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), selected);
  return widget;
}

GtkWidget *spin(int minimum, int maximum, int step, int selected) {
  const auto widget = gtk_spin_button_new_with_range(minimum, maximum, step);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), selected);
  return widget;
}

GtkWidget *file(const std::filesystem::path &path, bool folder = false) {
  const auto chooser = gtk_file_chooser_button_new(folder ? "Select folder" : "Select file",
                                                   folder ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
                                                          : GTK_FILE_CHOOSER_ACTION_OPEN);
  if (!path.empty())
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser), path.c_str());
  const auto widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start(GTK_BOX(widget), chooser, TRUE, TRUE, 0);
  const auto clear = gtk_button_new_with_label("Clear");
  gtk_box_pack_end(GTK_BOX(widget), clear, FALSE, FALSE, 0);
  g_signal_connect(clear, "clicked", G_CALLBACK(+[](GtkWidget *, gpointer data) {
                     gtk_file_chooser_unselect_all(GTK_FILE_CHOOSER(data));
                   }),
                   chooser);
  g_object_set_data(G_OBJECT(widget), "file-chooser", chooser);
  return widget;
}

std::filesystem::path selected_file(GtkWidget *widget) {
  auto selected = gtk_file_chooser_get_filename(
      GTK_FILE_CHOOSER(g_object_get_data(G_OBJECT(widget), "file-chooser")));
  std::filesystem::path result;
  if (selected) {
    result = selected;
    g_free(selected);
  }
  return result;
}

void row(GtkWidget *grid, unsigned number, const char *text, GtkWidget *widget) {
  const auto label = gtk_label_new(text);
  gtk_label_set_xalign(GTK_LABEL(label), 0);
  gtk_grid_attach(GTK_GRID(grid), label, 0, static_cast<int>(number), 1, 1);
  gtk_grid_attach(GTK_GRID(grid), widget, 1, static_cast<int>(number), 1, 1);
}

unsigned selection(GtkWidget *widget) {
  return static_cast<unsigned>(gtk_combo_box_get_active(GTK_COMBO_BOX(widget)));
}

unsigned number(GtkWidget *widget) {
  return static_cast<unsigned>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget)));
}

bool selected(GtkWidget *widget) {
  return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

} // namespace

void Window::settings_dialog() {
  modal_ = true;
  audio_.clear();
  input_.clear();
  const auto dialog =
      gtk_dialog_new_with_buttons("Settings", GTK_WINDOW(window_), GTK_DIALOG_MODAL, "Cancel",
                                  GTK_RESPONSE_CANCEL, "Apply", GTK_RESPONSE_APPLY, nullptr);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 680, 600);
  const auto notebook = gtk_notebook_new();
  gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), notebook, TRUE, TRUE,
                     0);
  const auto page = [&](const char *name) {
    const auto grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    const auto scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scroll), grid);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scroll, gtk_label_new(name));
    return grid;
  };
  const auto video = page("Video");
  const auto hardware = check("Hardware rendering", settings_.hardware);
  const auto render_scale = combo(
      {"1x", "2x", "4x", "8x"}, static_cast<unsigned>(std::find(n64::RenderSettings::scales.begin(),
                                                                n64::RenderSettings::scales.end(),
                                                                settings_.session.graphics.scale) -
                                                      n64::RenderSettings::scales.begin()));
  const auto supersampling = check("Supersampling", settings_.session.graphics.supersampling);
  const auto weave = check("Weave interlaced fields", settings_.session.graphics.weave);
  const auto bypass = check("Bypass VI filters", settings_.session.graphics.bypass_vi);
  const auto display_scale = combo({"Best fit", "Integer scaling", "Stretch to fill"},
                                   static_cast<unsigned>(settings_.scale));
  const auto aspect = combo({"Standard N64 (4:3)", "Original pixel dimensions"},
                            static_cast<unsigned>(settings_.aspect));
  row(video, 0, "Renderer (next game load)", hardware);
  row(video, 1, "Rendering resolution", render_scale);
  row(video, 2, "", supersampling);
  row(video, 3, "", weave);
  row(video, 4, "", bypass);
  row(video, 5, "Display scaling", display_scale);
  row(video, 6, "Aspect ratio", aspect);
  std::array<GtkWidget *, 4> rendering_options{render_scale, supersampling, weave, bypass};
  const auto renderer = +[](GtkToggleButton *hardware, gpointer data) {
    const auto &options = *static_cast<std::array<GtkWidget *, 4> *>(data);
    const bool enabled = gtk_toggle_button_get_active(hardware);
    for (const auto option : options)
      gtk_widget_set_sensitive(option, enabled);
    if (!enabled) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(options[0]), 0);
      for (unsigned n = 1; n < options.size(); ++n)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(options[n]), FALSE);
    }
  };
  g_signal_connect(hardware, "toggled", G_CALLBACK(renderer), &rendering_options);
  renderer(GTK_TOGGLE_BUTTON(hardware), &rendering_options);
  const auto sound = page("Audio");
  const auto volume = spin(0, 200, 5, static_cast<int>(settings_.audio.levels.volume));
  const auto balance = spin(-100, 100, 5, settings_.audio.levels.balance);
  const auto latency = spin(20, 320, 10, static_cast<int>(settings_.audio.latency));
  const auto muted = check("Mute", settings_.muted);
  const auto device = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(device), "", "System default");
  const auto preferred_device = utf8(settings_.audio.device);
  auto devices = audio_devices();
  if (!preferred_device.empty() &&
      std::find(devices.begin(), devices.end(), preferred_device) == devices.end())
    devices.push_back(preferred_device);
  for (const auto &name : devices)
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(device), name.c_str(), name.c_str());
  gtk_combo_box_set_active_id(GTK_COMBO_BOX(device), preferred_device.c_str());
  row(sound, 0, "Volume (%)", volume);
  row(sound, 1, "Balance (left to right)", balance);
  row(sound, 2, "Latency (ms)", latency);
  row(sound, 3, "Output device", device);
  row(sound, 4, "", muted);
  const auto playback = page("Playback");
  const auto sync = combo({"Audio", "Video"}, static_cast<unsigned>(settings_.sync));
  const auto focus =
      combo({"Pause", "Block input", "Allow input"}, static_cast<unsigned>(settings_.focus));
  const auto gdb = check("Enable GDB connection on localhost", settings_.gdb_enabled);
  const auto port = spin(1, 65535, 1, static_cast<int>(settings_.gdb_port));
  row(playback, 0, "Synchronization", sync);
  row(playback, 1, "When the window loses focus", focus);
  row(playback, 2, "Debugger", gdb);
  row(playback, 3, "GDB port", port);
  const auto storage = page("Files");
  const auto pif = file(settings_.firmware.n64), arcade_pif = file(settings_.firmware.arcade),
             ipl = file(settings_.firmware.ipl);
  const auto saves = file(settings_.session.storage.saves, true),
             screenshots = file(settings_.session.storage.screenshots, true),
             firmware = file(settings_.session.storage.firmware, true),
             debugging = file(settings_.session.storage.debugging, true);
  row(storage, 0, "Nintendo 64 PIF firmware", pif);
  row(storage, 1, "Aleck64 PIF firmware", arcade_pif);
  row(storage, 2, "64DD IPL firmware", ipl);
  row(storage, 3, "Game saves", saves);
  row(storage, 4, "Screenshots", screenshots);
  row(storage, 5, "Firmware folder", firmware);
  row(storage, 6, "Debugger exports", debugging);
  row(storage, 7, "",
      gtk_label_new(
          "Empty output folders use the game folder. Save folder changes apply on game load."));
  const auto controls = page("Controllers");
  const auto expansion = check("Expansion Pak (8 MiB)", settings_.session.memory.expansion);
  const auto feedback = check("Controller rumble", settings_.feedback);
  row(controls, 0, "Memory (next game load)", expansion);
  row(controls, 1, "Feedback", feedback);
  row(controls, 2, "",
      gtk_label_new("Controller types and accessories apply on the next game load."));
  struct PortWidgets {
    GtkWidget *type, *accessory, *source, *banks, *cartridge, *rate;
  };
  std::array<PortWidgets, 4> ports{};
  auto mappings = settings_.mappings;
  struct MappingDialog {
    Window *window;
    unsigned port;
    ControllerMapping *mapping;
    GtkWidget *source;
  };
  std::array<MappingDialog, 4> mapping_dialogs{};
  for (unsigned n = 0; n < ports.size(); ++n) {
    const auto &configured = settings_.session.ports[n];
    auto &widgets = ports[n];
    widgets.type = combo({"Automatic", "Disconnected", "Nintendo 64", "Mouse", "GameCube"},
                         static_cast<unsigned>(configured.controller));
    widgets.accessory =
        combo({"Automatic", "None", "Controller Pak", "Rumble Pak", "Transfer Pak", "Bio Sensor"},
              static_cast<unsigned>(configured.accessory));
    widgets.source = combo({"None", "Keyboard", "Gamepad 1", "Gamepad 2", "Gamepad 3", "Gamepad 4"},
                           static_cast<unsigned>(mappings[n].source));
    widgets.banks = combo({"32 KiB", "128 KiB", "512 KiB", "1984 KiB"},
                          static_cast<unsigned>(std::find(MemorySettings::capacities.begin(),
                                                          MemorySettings::capacities.end(),
                                                          settings_.session.memory.pak_banks[n]) -
                                                MemorySettings::capacities.begin()));
    widgets.cartridge = file(configured.cartridge);
    widgets.rate = spin(30, 180, 1, static_cast<int>(configured.heart_rate));
    const auto start = 3 + n * 8;
    const auto label = "Port " + std::to_string(n + 1);
    row(controls, start, label.c_str(), widgets.type);
    row(controls, start + 1, "Accessory", widgets.accessory);
    row(controls, start + 2, "Host input", widgets.source);
    row(controls, start + 3, "Controller Pak capacity", widgets.banks);
    row(controls, start + 4, "Transfer Pak cartridge", widgets.cartridge);
    row(controls, start + 5, "Heart rate", widgets.rate);
    const auto button = gtk_button_new_with_label("Remap buttons and axes…");
    mapping_dialogs[n] = {this, n, &mappings[n], widgets.source};
    g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkWidget *, gpointer data) {
                       auto &selection = *static_cast<MappingDialog *>(data);
                       const auto source = static_cast<HostSource>(
                           gtk_combo_box_get_active(GTK_COMBO_BOX(selection.source)));
                       if (source != selection.mapping->source)
                         *selection.mapping = default_mapping(source);
                       selection.window->mapping_dialog(selection.port, *selection.mapping);
                     }),
                     &mapping_dialogs[n]);
    row(controls, start + 6, "Bindings", button);
  }
  gtk_widget_show_all(dialog);
  while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_APPLY) {
    try {
      Settings next = settings_;
      next.hardware = selected(hardware);
      next.session.graphics = {n64::RenderSettings::scales.at(selection(render_scale)),
                               selected(supersampling), selected(weave), selected(bypass)};
      next.scale = static_cast<DisplayScale>(selection(display_scale));
      next.aspect = static_cast<DisplayAspect>(selection(aspect));
      next.audio.levels = {number(volume),
                           gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(balance))};
      next.audio.latency = number(latency);
      const auto device_name = gtk_combo_box_get_active_id(GTK_COMBO_BOX(device));
      next.audio.device = device_name ? wide(device_name) : std::wstring{};
      next.muted = selected(muted);
      next.sync = static_cast<PlaybackSync>(selection(sync));
      next.focus = static_cast<FocusBehavior>(selection(focus));
      next.gdb_enabled = selected(gdb);
      next.gdb_port = number(port);
      next.firmware = {selected_file(pif), selected_file(arcade_pif), selected_file(ipl)};
      next.session.storage = {selected_file(saves), selected_file(screenshots),
                              selected_file(firmware), selected_file(debugging)};
      next.session.memory.expansion = selected(expansion);
      next.feedback = selected(feedback);
      next.mappings = mappings;
      for (unsigned n = 0; n < ports.size(); ++n) {
        const auto &widgets = ports[n];
        next.session.ports[n] = {static_cast<ControllerType>(selection(widgets.type)),
                                 static_cast<AccessoryType>(selection(widgets.accessory)),
                                 selected_file(widgets.cartridge), number(widgets.rate)};
        next.session.memory.pak_banks[n] = MemorySettings::capacities.at(selection(widgets.banks));
        const auto source = static_cast<HostSource>(selection(widgets.source));
        if (next.mappings[n].source != source)
          next.mappings[n] = default_mapping(source);
      }
      next.validate();
      for (const auto type :
           {FirmwareType::Nintendo64, FirmwareType::Aleck64, FirmwareType::DiskIpl}) {
        const auto &path = type == FirmwareType::Nintendo64 ? next.firmware.n64
                           : type == FirmwareType::Aleck64  ? next.firmware.arcade
                                                            : next.firmware.ipl;
        if (!path.empty())
          validate_firmware(path, type);
      }
      for (const auto location : {StorageLocation::Saves, StorageLocation::Screenshots,
                                  StorageLocation::Firmware, StorageLocation::Debugging}) {
        const auto &path = next.session.storage.selected(location);
        if (!path.empty())
          validate_directory(path, location != StorageLocation::Firmware);
      }
      if (next.gdb_enabled && (!gdb_.port() || next.gdb_port != gdb_.port()))
        gdb_.listen(next.gdb_port);
      if (!next.gdb_enabled)
        gdb_.close();
      if (!options_.frames && !settings_path_.empty())
        save_settings(settings_path_, next);
      if (session_ && session_->hardware_rendering())
        session_->graphics_settings(next.session.graphics);
      settings_ = std::move(next);
      audio_.configure(settings_.audio);
      audio_.muted = settings_.muted;
      break;
    } catch (const std::exception &failure) {
      error(failure);
    }
  }
  gtk_widget_destroy(dialog);
  modal_ = false;
  clear_input();
  audio_.clear();
  poll_gdb();
  rebase();
  refresh();
}

} // namespace cupid::desktop::linux_host
