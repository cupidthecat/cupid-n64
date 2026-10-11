#include "desktop/session.hpp"
#include "core/cartridge/profile/profile.hpp"
#include "desktop/state/session_state.hpp"
#include <algorithm>
#include <stdexcept>

namespace cupid::desktop {

Session::Session(const std::filesystem::path &rom, const std::filesystem::path &firmware,
                 Audio &audio, const std::filesystem::path &ipl, const std::filesystem::path &disk,
                 n64::ArcadeProfile arcade, std::optional<std::uint64_t> random_seed,
                 bool hardware_rendering, SessionSettings settings)
    : audio_(audio), settings_(std::move(settings)), cartridge_image_(rom), ipl_image_(ipl),
      hardware_rendering_(hardware_rendering) {
  validate_ports(settings_.ports);
  if (!settings_.graphics.valid())
    throw std::runtime_error("Select a supported rendering configuration.");
  for (const auto banks : settings_.memory.pak_banks)
    if (std::find(MemorySettings::capacities.begin(), MemorySettings::capacities.end(), banks) ==
        MemorySettings::capacities.end())
      throw std::runtime_error("Select a supported Controller Pak capacity.");
  if (!settings_.storage.saves.empty())
    validate_directory(settings_.storage.saves, true);
  auto cartridge = rom.empty() ? std::vector<std::uint8_t>{} : read_file(rom, 0x0fc00000);
  auto pif = read_file(firmware, arcade == n64::ArcadeProfile::Disabled ? 0x7c0 : 0x800);
  const auto profile =
      rom.empty() ? std::optional(n64::CartridgeProfile{}) : n64::inspect_cartridge(cartridge);
  if (!profile)
    throw std::runtime_error("Select a valid Nintendo 64 cartridge image.");
  n64::ConsoleConfig config;
  config.expansion = settings_.memory.expansion;
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
  const auto cartridge_save =
      settings_.storage.save_image(rom, n64::state::Archive::fingerprint(cartridge));
  storage_image_ = cartridge_save;
  if (config.disk_drive) {
    const auto ipl_data = read_file(ipl, 0x400000);
    if (!console_->disk_drive().load_ipl(ipl_data))
      throw std::runtime_error("Select a valid 64DD IPL firmware file.");
    ipl_identity_ = n64::state::Archive::fingerprint(ipl_data);
    const auto ipl_save = settings_.storage.save_image(ipl, ipl_identity_);
    if (rom.empty())
      storage_image_ = ipl_save;
    clock_path_ = save_path(ipl_save, L".rtc");
    if (std::filesystem::exists(clock_path_) &&
        !console_->disk_drive().clock().load(read_file(clock_path_, 16)))
      throw std::runtime_error("The disk clock save must contain 16 bytes.");
    const auto clock = console_->disk_drive().clock().save();
    saved_clock_.assign(clock.begin(), clock.end());
    if (!disk.empty())
      change_disk(disk);
  }
  const auto controller_save = rom.empty() ? storage_image_ : cartridge_save;
  configure_ports(profile->accessories, controller_save);
  if (!console_->load(cartridge, pif))
    throw std::runtime_error(
        arcade == n64::ArcadeProfile::Disabled
            ? "Select a game or 64DD IPL and a 1984-byte PIF firmware file."
            : "Select an Aleck64 game and its 1984- or 2048-byte PIF firmware file.");
  if (!rom.empty()) {
    const auto attach_save = [&](const wchar_t *extension, std::span<std::uint8_t> memory) {
      if (!memory.empty())
        saves_.emplace_back(save_path(cartridge_save, extension), memory);
    };
    attach_save(L".eep", console_->eeprom().data());
    attach_save(L".sra", console_->sram().data());
    attach_save(L".fla", console_->flash().data());
    if (config.rtc_present) {
      cartridge_clock_path_ = save_path(cartridge_save, L".rtc");
      if (std::filesystem::exists(cartridge_clock_path_) &&
          !console_->rtc().load(read_file(cartridge_clock_path_, 32)))
        throw std::runtime_error("The cartridge clock save must contain 32 bytes.");
      const auto clock = console_->rtc().save();
      saved_cartridge_clock_.assign(clock.begin(), clock.end());
    }
  }
  output_ = std::make_unique<VideoOutput>(*console_, hardware_rendering_, settings_.graphics);
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
  clear_mouse_input();
  output_.reset();
  frame_pending_ = false;
  console_->power(true);
  audio_.clear();
  audio_.frequency(console_->audio().frequency());
  output_ = std::make_unique<VideoOutput>(*console_, hardware_rendering_, settings_.graphics);
  frame = {};
  frames = 0;
}

void Session::finish_frame() {
  if (frame_pending_) {
    frame = output_->read_frame();
    frame_pending_ = false;
  }
}

void Session::advance_frame(std::uint16_t buttons, std::int8_t x, std::int8_t y) {
  legacy_input(buttons, x, y);
  advance_to_frame();
}

void Session::advance_frame(const PlayerInputs &inputs) {
  input(inputs);
  advance_to_frame();
}

void Session::advance_to_frame() {
  struct QuietAudio {
    Audio &audio;
    bool muted;
    ~QuietAudio() {
      audio.clear();
      audio.muted = muted;
    }
  } quiet{audio_, audio_.muted};
  audio_.clear();
  audio_.muted = true;
  const auto before = frames;
  const auto started = clocks();
  while (frames == before) {
    run_interval(true);
    if (clocks() - started > 10 * n64::clock_frequency)
      throw std::runtime_error("Frame advance stopped because no video frame was produced.");
  }
  finish_frame();
}

void Session::run(std::uint16_t buttons, std::int8_t x, std::int8_t y) {
  legacy_input(buttons, x, y);
  run_interval(false);
}

void Session::run(const PlayerInputs &inputs) {
  input(inputs);
  run_interval(false);
}

void Session::run() {
  run_interval(false);
}

void Session::graphics_settings(n64::RenderSettings settings) {
  output_->scanout_settings(settings);
  settings_.graphics = settings;
}

void Session::run_interval(bool frame_boundary) {
  if (frame_boundary)
    console_->run_frame_interval();
  else
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
  save_transfers();
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

std::vector<std::uint8_t> Session::capture_state() {
  return SessionState::capture(*this);
}

void Session::restore_state(std::span<const std::uint8_t> bytes) {
  SessionState::restore(*this, bytes);
  clear_mouse_input();
}

} // namespace cupid::desktop
