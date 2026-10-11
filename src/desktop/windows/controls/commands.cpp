#include "desktop/windows/commands.hpp"
#include "desktop/windows/controls/mapping_dialog.hpp"
#include "desktop/windows/controls/ports_dialog.hpp"
#include "desktop/windows/window.hpp"

namespace cupid::desktop {

std::wstring Window::selected_controller_key() const {
  return controller_settings_key(!options_.rom.empty()    ? options_.rom
                                 : !options_.disk.empty() ? options_.disk
                                                          : options_.ipl);
}

bool Window::stage_controller_settings() {
  const auto key = selected_controller_key();
  if (key == controller_key_)
    return false;
  auto ports = options_.frames ? ControllerPorts{} : read_ports(settings_, key);
  const bool changed = ports != session_settings_.ports;
  controller_key_ = key;
  session_settings_.ports = std::move(ports);
  reload_pending_ |= changed;
  return changed;
}

void Window::create_controller_menus(HMENU emulation, HMENU view) {
  AppendMenuW(emulation, MF_STRING | MF_GRAYED, ControllerSetup,
              L"Controller &ports for this game...");
  AppendMenuW(view, MF_STRING, InputMappings, L"&Input mappings for four players...");
  AppendMenuW(view, MF_STRING, Shortcuts, L"&Shortcuts...");
  AppendMenuW(view, MF_STRING | MF_GRAYED, MouseCapture, L"&Capture mouse pointer");
}

void Window::update_controllers() {
  if (menu_) {
    EnableMenuItem(menu_, ControllerSetup,
                   MF_BYCOMMAND |
                       (session_ && options_.arcade_profile == n64::ArcadeProfile::Disabled
                            ? MF_ENABLED
                            : MF_GRAYED));
    DrawMenuBar(window_);
  }
}

void Window::controller_dialog() {
  PortForm form;
  form.settings = session_settings_.ports;
  form.defaults = session_->automatic_ports();
  for (unsigned port = 0; port < form.current.size(); ++port)
    form.current[port] = session_->port_status(port);
  form.directory = (options_.rom.empty() ? options_.ipl : options_.rom).parent_path();
  if (auto selected = choose_ports(window_, std::move(form));
      selected && *selected != session_settings_.ports) {
    save_ports(settings_, controller_key_, *selected);
    const auto effective = resolve_ports(*selected, session_->automatic_ports());
    for (unsigned port = 0; port < effective.size(); ++port)
      if (session_->port_status(port).accessory == AccessoryType::BioSensor &&
          effective[port].accessory == AccessoryType::BioSensor)
        session_->heart_rate(port, (*selected)[port].heart_rate);
    session_settings_.ports = std::move(*selected);
    reload_pending_ = true;
    state_message_ = L"Controller ports saved; reload the game to apply the setup";
  }
}

bool Window::controller_command(unsigned id) {
  if (id != ControllerSetup && id != InputMappings)
    return false;
  end_rewind();
  stop_fast_forward();
  gamepads_.stop();
  clear_host_input();
  try {
    if (id == InputMappings)
      mapping_dialog();
    else if (session_ && options_.arcade_profile == n64::ArcadeProfile::Disabled)
      controller_dialog();
  } catch (const std::exception &error) {
    status_error(L"Controller settings failed", error);
  }
  rebase();
  update_launch();
  update_controllers();
  return true;
}

void Window::mapping_dialog() {
  MappingForm form;
  form.mappings = player_mappings_;
  form.available = gamepads_.available();
  form.poll = [this] { return gamepads_.poll({true, true, true, true}); };
  if (session_)
    for (unsigned port = 0; port < form.controllers.size(); ++port)
      form.controllers[port] = session_->port_status(port);
  if (auto selected = choose_mappings(window_, std::move(form));
      selected && *selected != player_mappings_) {
    if (!options_.frames)
      save_mappings(settings_, *selected);
    player_mappings_ = std::move(*selected);
    state_message_ = L"Input mappings saved";
  }
}

HostInputs Window::host_inputs() const {
  auto host = last_host_inputs_;
  host.keys.fill(false);
  if (active_)
    host.keys = keys_;
  else if (focus_behavior_ == FocusBehavior::AllowInput)
    for (unsigned key = 1; key < host.keys.size(); ++key)
      host.keys[key] = (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0;
  return host;
}

PlayerInputs Window::inputs() {
  std::array<bool, 4> wanted{};
  for (const auto &mapping : player_mappings_)
    if (mapping.source >= HostSource::Gamepad1 && mapping.source <= HostSource::Gamepad4)
      wanted[static_cast<unsigned>(mapping.source) - static_cast<unsigned>(HostSource::Gamepad1)] =
          true;
  last_host_inputs_.gamepads = gamepads_.poll(wanted);
  last_host_inputs_ = host_inputs();
  PlayerInputs players;
  const auto mouse = mouse_.input();
  if (active_ || focus_behavior_ == FocusBehavior::AllowInput)
    for (unsigned port = 0; port < players.size(); ++port) {
      if (session_ && session_->port_status(port).controller == ControllerType::Mouse) {
        if (active_ && mouse_.captured() && !stopped() && !rewinding_)
          players[port] = mouse;
      } else {
        players[port] = map_input(player_mappings_[port], last_host_inputs_);
      }
    }
  if (options_.test_input && session_) {
    const auto scripted = input();
    players[0].buttons = scripted.buttons;
    players[0].x = scripted.x;
    players[0].y = scripted.y;
    players[0].calibrated_stick = true;
  }
  return players;
}

std::array<FeedbackState, 4> Window::feedback_state() const {
  std::array<FeedbackState, 4> state{};
  if (session_)
    for (unsigned port = 0; port < state.size(); ++port)
      state[port] = session_->feedback_state(port);
  return state;
}

void Window::update_feedback() {
  const auto motors = feedback_.update(feedback_state());
  gamepads_.rumble(feedback_targets(player_mappings_, motors, last_host_inputs_,
                                    session_ && !stopped() && !rewinding_));
}

} // namespace cupid::desktop
