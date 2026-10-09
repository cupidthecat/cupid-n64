#include "desktop/session.hpp"
#include "core/cartridge/profile/profile.hpp"
#include <algorithm>
#include <stdexcept>

namespace cupid::desktop {

Session::Session(const std::filesystem::path &rom, const std::filesystem::path &firmware,
                 Audio &audio, const std::filesystem::path &ipl, const std::filesystem::path &disk,
                 n64::ArcadeProfile arcade, std::optional<std::uint64_t> random_seed,
                 bool hardware_rendering)
    : audio_(audio), hardware_rendering_(hardware_rendering) {
  auto cartridge = rom.empty() ? std::vector<std::uint8_t>{} : read_file(rom, 0x0fc00000);
  auto pif = read_file(firmware, arcade == n64::ArcadeProfile::Disabled ? 0x7c0 : 0x800);
  const auto profile =
      rom.empty() ? std::optional(n64::CartridgeProfile{}) : n64::inspect_cartridge(cartridge);
  if (!profile)
    throw std::runtime_error("Select a valid Nintendo 64 cartridge image.");
  n64::ConsoleConfig config;
  config.random_seed = random_seed;
  config.arcade_profile = arcade;
  if (arcade != n64::ArcadeProfile::Disabled) {
    if (rom.empty() || !ipl.empty() || !disk.empty())
      throw std::runtime_error("Aleck64 requires a cartridge image and its PIF firmware.");
  } else {
    config.region = profile->region;
    config.cic = profile->cic;
    config.eeprom_size = profile->eeprom_size;
    config.sram_size = profile->sram_size;
    config.flash_model = profile->flash_model;
    config.rtc_present = profile->rtc_present;
    config.disk_drive = !ipl.empty();
  }
  console_ = std::make_unique<n64::Console>(config);
  if (config.disk_drive) {
    if (!console_->disk_drive().load_ipl(read_file(ipl, 0x400000)))
      throw std::runtime_error("Select a valid 64DD IPL firmware file.");
    clock_path_ = save_path(ipl, L".rtc");
    if (std::filesystem::exists(clock_path_) &&
        !console_->disk_drive().clock().load(read_file(clock_path_, 16)))
      throw std::runtime_error("The disk clock save must contain 16 bytes.");
    const auto clock = console_->disk_drive().clock().save();
    saved_clock_.assign(clock.begin(), clock.end());
    if (!disk.empty()) {
      if (!console_->disk_drive().load_image(read_file(disk, 0x435b0c0)))
        throw std::runtime_error("Select a valid 64DD disk image.");
      disk_path_ = save_path(disk, L".disk");
      if (std::filesystem::exists(disk_path_)) {
        const auto errors = console_->disk_drive().disk_errors();
        if (!console_->disk_drive().insert(read_file(disk_path_, 0x435b0c0), errors))
          throw std::runtime_error("The disk save has an invalid size.");
      }
      const auto data = console_->disk_drive().disk_data();
      saved_disk_.assign(data.begin(), data.end());
    }
  }
  for (unsigned port = 0; !console_->arcade() && port < profile->accessories.size(); ++port) {
    auto &pad = console_->controller(port);
    switch (profile->accessories[port]) {
    case n64::ControllerAccessory::Memory:
      pad.memory_pak();
      break;
    case n64::ControllerAccessory::Rumble:
      pad.rumble_pak();
      break;
    case n64::ControllerAccessory::Transfer:
      pad.transfer_pak();
      break;
    case n64::ControllerAccessory::None:
      break;
    }
    console_->connect_controller(port, port == 0 || profile->accessories[port] !=
                                                        n64::ControllerAccessory::None);
  }
  if (!console_->load(cartridge, pif))
    throw std::runtime_error(
        arcade == n64::ArcadeProfile::Disabled
            ? "Select a game or 64DD IPL and a 1984-byte PIF firmware file."
            : "Select an Aleck64 game and its 1984- or 2048-byte PIF firmware file.");
  if (!rom.empty()) {
    const auto attach_save = [&](const wchar_t *extension, std::span<std::uint8_t> memory) {
      if (!memory.empty())
        saves_.emplace_back(save_path(rom, extension), memory);
    };
    attach_save(L".eep", console_->eeprom().data());
    attach_save(L".sra", console_->sram().data());
    attach_save(L".fla", console_->flash().data());
    constexpr const wchar_t *pak_extensions[]{L".pak", L".p2.pak", L".p3.pak", L".p4.pak"};
    for (unsigned port = 0; port < 4; ++port)
      attach_save(pak_extensions[port], console_->controller(port).pak_data());
    if (config.rtc_present) {
      cartridge_clock_path_ = save_path(rom, L".rtc");
      if (std::filesystem::exists(cartridge_clock_path_) &&
          !console_->rtc().load(read_file(cartridge_clock_path_, 32)))
        throw std::runtime_error("The cartridge clock save must contain 32 bytes.");
      const auto clock = console_->rtc().save();
      saved_cartridge_clock_.assign(clock.begin(), clock.end());
    }
  }
  output_ = std::make_unique<VideoOutput>(*console_, hardware_rendering_);
  console_->audio().connect([this](n64::StereoSample sample) { audio_.sample(sample); },
                            [this](unsigned rate) { audio_.frequency(rate); });
  console_->video().connect_frame([this](bool field) {
    finish_frame();
    output_->begin_frame(field);
    frame_pending_ = true;
    ++frames;
  });
}

void Session::reset() {
  output_.reset();
  frame_pending_ = false;
  console_->power(true);
  audio_.clear();
  audio_.frequency(console_->audio().frequency());
  output_ = std::make_unique<VideoOutput>(*console_, hardware_rendering_);
  frame = {};
  frames = 0;
}

void Session::finish_frame() {
  if (frame_pending_) {
    frame = output_->read_frame();
    frame_pending_ = false;
  }
}

void Session::run(std::uint16_t buttons, std::int8_t x, std::int8_t y) {
  if (auto *arcade = console_->arcade()) {
    auto &player = arcade->input().players[0];
    player.up = buttons & 0x0800;
    player.down = buttons & 0x0400;
    player.left = buttons & 0x0200;
    player.right = buttons & 0x0100;
    player.buttons[0] = buttons & 0x8000;
    player.buttons[1] = buttons & 0x4000;
    player.buttons[2] = buttons & 0x0010;
    player.buttons[3] = buttons & 0x0001;
    player.start = buttons & 0x1000;
    player.coin = buttons & 0x2000;
    player.x = x;
    player.y = y;
  } else {
    console_->controller(0).input(buttons, x, y);
  }
  console_->run_interval();
  if (output_->requires_hardware())
    throw std::runtime_error(
        "Hardware rendering is unavailable. This game requires it for RDP graphics.");
  if (console_->frozen() || output_->crashed() || console_->pif().state() == n64::Pif::State::Error)
    throw std::runtime_error(
        "Emulation stopped. Check the game and firmware files, then use Reset.");
}

void Session::save() {
  for (auto &memory : saves_)
    memory.save();
  if (!cartridge_clock_path_.empty()) {
    const auto clock = console_->rtc().save();
    if (!std::equal(clock.begin(), clock.end(), saved_cartridge_clock_.begin())) {
      write_save(cartridge_clock_path_, clock);
      saved_cartridge_clock_.assign(clock.begin(), clock.end());
    }
  }
  if (!clock_path_.empty()) {
    const auto clock = console_->disk_drive().clock().save();
    if (!std::equal(clock.begin(), clock.end(), saved_clock_.begin())) {
      write_save(clock_path_, clock);
      saved_clock_.assign(clock.begin(), clock.end());
    }
  }
  if (!disk_path_.empty()) {
    const auto disk = console_->disk_drive().disk_data();
    if (!std::equal(disk.begin(), disk.end(), saved_disk_.begin())) {
      write_save(disk_path_, disk);
      saved_disk_.assign(disk.begin(), disk.end());
    }
  }
}

} // namespace cupid::desktop
