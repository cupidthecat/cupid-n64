#include "core/controller/stick/response.hpp"
#include "core/state/archive.hpp"
#include "desktop/session.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace cupid::desktop {
namespace {

std::wstring pak_extension(unsigned port, unsigned banks) {
  const auto player = port ? L".p" + std::to_wstring(port + 1) : std::wstring{};
  return player + (banks == 1 ? std::wstring{} : L"." + std::to_wstring(banks * 32) + L"k") +
         L".pak";
}

void arcade_input(n64::Aleck64::Player &player, const PortInput &input) {
  player.up = input.buttons & 0x0800;
  player.down = input.buttons & 0x0400;
  player.left = input.buttons & 0x0200;
  player.right = input.buttons & 0x0100;
  player.buttons[0] = input.buttons & 0x8000;
  player.buttons[1] = input.buttons & 0x4000;
  player.buttons[2] = input.buttons & 0x0010;
  player.buttons[3] = input.buttons & 0x0001;
  player.start = input.buttons & 0x1000;
  player.coin = input.buttons & 0x2000;
  if (input.calibrated_stick) {
    player.x = static_cast<std::int8_t>(std::clamp(int(input.x), -128, 127));
    player.y = static_cast<std::int8_t>(std::clamp(int(input.y), -128, 127));
  } else {
    const auto stick = n64::stick_response(input.x, input.y);
    player.x = stick.x;
    player.y = -stick.y;
  }
}

} // namespace

void Session::configure_ports(const std::array<n64::ControllerAccessory, 4> &defaults,
                              const std::filesystem::path &save_image) {
  automatic_ports_ = default_ports(defaults);
  ports_ = resolve_ports(settings_.ports, automatic_ports_);
  for (unsigned port = 0; port < ports_.size(); ++port) {
    auto &status = ports_[port];
    const auto &selected = settings_.ports[port];
    if (console_->arcade()) {
      status.controller = port < 2 ? ControllerType::Nintendo64 : ControllerType::Disconnected;
      status.accessory = AccessoryType::None;
      status.cartridge.clear();
      automatic_ports_[port] = status;
      continue;
    }
    auto &pad = console_->controller(port);
    switch (status.accessory) {
    case AccessoryType::Memory:
      pad.memory_pak(settings_.memory.pak_banks[port]);
      saves_.emplace_back(
          save_path(save_image, pak_extension(port, settings_.memory.pak_banks[port]).c_str()),
          pad.pak_data());
      break;
    case AccessoryType::Rumble:
      pad.rumble_pak();
      break;
    case AccessoryType::Transfer:
      if (selected.cartridge.empty())
        pad.transfer_pak();
      else
        attach_transfer(port, selected.cartridge);
      break;
    case AccessoryType::BioSensor:
      pad.bio_sensor();
      pad.sensor().beats_per_minute(selected.heart_rate);
      break;
    case AccessoryType::Automatic:
    case AccessoryType::None:
      break;
    }
    if (status.controller == ControllerType::Mouse) {
      console_->mouse(port).input_reader([this, port] {
        const auto input = mouse_motion_[port].read();
        return n64::Mouse::Input{input.mouse_left, input.mouse_right, input.mouse_x, input.mouse_y};
      });
      console_->connect_mouse(port);
    } else if (status.controller == ControllerType::GameCube)
      console_->connect_gamecube_controller(port);
    else
      console_->connect_controller(port, status.controller == ControllerType::Nintendo64);
  }
}

void Session::attach_transfer(unsigned port, const std::filesystem::path &path) {
  const auto bytes = read_file(path, 0x1000000);
  auto &media = transfer_media_.at(port);
  media.cartridge = std::make_shared<n64::HandheldCartridge>();
  if (!media.cartridge->load(bytes))
    throw std::runtime_error("Select a valid Game Boy or Game Boy Color cartridge image.");
  const auto fingerprint = n64::state::Archive::fingerprint(bytes);
  const auto image = settings_.storage.save_image(path, fingerprint);
  std::wostringstream identity;
  identity << L".p" << port + 1 << L"." << std::hex << std::setfill(L'0') << std::setw(16)
           << fingerprint;
  const auto attach = [&](const wchar_t *extension, std::span<std::uint8_t> memory) {
    if (!memory.empty())
      saves_.emplace_back(save_path(image, (identity.str() + extension).c_str()), memory);
  };
  attach(L".ram", media.cartridge->save_ram());
  attach(L".eep", media.cartridge->eeprom());
  attach(L".fla", media.cartridge->flash());
  const auto clock = media.cartridge->save_clock();
  if (!clock.empty()) {
    media.clock_path = save_path(image, (identity.str() + L".rtc").c_str());
    if (std::filesystem::exists(media.clock_path) &&
        !media.cartridge->load_clock(read_file(media.clock_path, 15)))
      throw std::runtime_error("The handheld cartridge clock save has an invalid format.");
    media.saved_clock = media.cartridge->save_clock();
  }
  console_->controller(port).transfer_pak(media.cartridge);
  ports_[port].cartridge = path;
}

void Session::save_transfers() {
  for (auto &media : transfer_media_) {
    if (media.clock_path.empty())
      continue;
    const auto clock = media.cartridge->save_clock();
    if (clock != media.saved_clock) {
      write_save(media.clock_path, clock);
      media.saved_clock = clock;
    }
  }
}

void Session::input(const PlayerInputs &inputs) {
  if (auto *arcade = console_->arcade()) {
    for (unsigned port = 0; port < arcade->input().players.size(); ++port)
      arcade_input(arcade->input().players[port], inputs[port]);
    return;
  }
  for (unsigned port = 0; port < inputs.size(); ++port) {
    const auto &value = inputs[port];
    switch (ports_[port].controller) {
    case ControllerType::Nintendo64:
      if (value.calibrated_stick)
        console_->controller(port).input(
            value.buttons, static_cast<std::int8_t>(std::clamp(int(value.x), -128, 127)),
            static_cast<std::int8_t>(std::clamp(int(value.y), -128, 127)));
      else
        console_->controller(port).input_host(value.buttons, value.x, value.y);
      break;
    case ControllerType::Mouse:
      mouse_motion_[port].input(value.mouse_left, value.mouse_right, value.mouse_x, value.mouse_y);
      break;
    case ControllerType::GameCube:
      console_->gamecube_controller(port).input_host(
          {value.gamecube_buttons, value.x, value.y, value.cx, value.cy, value.l, value.r});
      break;
    case ControllerType::Automatic:
    case ControllerType::Disconnected:
      break;
    }
  }
}

void Session::clear_mouse_input() {
  for (auto &motion : mouse_motion_)
    motion.clear();
}

void Session::legacy_input(std::uint16_t buttons, std::int8_t x, std::int8_t y) {
  if (auto *arcade = console_->arcade()) {
    PortInput input;
    input.buttons = buttons;
    input.x = x;
    input.y = y;
    input.calibrated_stick = true;
    arcade_input(arcade->input().players[0], input);
  } else {
    console_->controller(0).input(buttons, x, y);
  }
}

FeedbackState Session::feedback_state(unsigned port) const {
  const auto &status = ports_.at(port);
  if (status.controller == ControllerType::GameCube) {
    const auto &pad = console_->gamecube_controller(port);
    return {pad.rumbling(), pad.rumble_commands()};
  }
  if (status.controller != ControllerType::Nintendo64)
    return {};
  if (status.accessory == AccessoryType::Transfer && transfer_media_[port].cartridge) {
    const auto &cartridge = *transfer_media_[port].cartridge;
    return {cartridge.rumbling(), cartridge.rumble_commands()};
  }
  const auto &pad = console_->controller(port);
  return {pad.rumbling(), pad.rumble_commands()};
}

void Session::heart_rate(unsigned port, unsigned value) {
  if (value < 30 || value > 180)
    throw std::runtime_error("Bio Sensor heart rate must be from 30 through 180 BPM.");
  if (ports_.at(port).accessory != AccessoryType::BioSensor)
    throw std::runtime_error("Select a Bio Sensor on this controller port first.");
  console_->controller(port).sensor().beats_per_minute(value);
  ports_[port].heart_rate = settings_.ports[port].heart_rate = value;
}

} // namespace cupid::desktop
