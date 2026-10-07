#include "desktop/session.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace cupid::desktop {
namespace {
std::vector<std::uint8_t> read(const std::filesystem::path &path, std::size_t maximum) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream)
    throw std::runtime_error("Could not open the selected file.");
  const auto length = stream.tellg();
  if (length < 0 || static_cast<std::uint64_t>(length) > maximum)
    throw std::runtime_error("The selected file has an invalid size.");
  std::vector<std::uint8_t> data(static_cast<std::size_t>(length));
  stream.seekg(0);
  if (!stream.read(reinterpret_cast<char *>(data.data()), length))
    throw std::runtime_error("Could not read the selected file.");
  return data;
}

void write_save(const std::filesystem::path &path, std::span<const std::uint8_t> data) {
  auto temporary = path;
  temporary += L".tmp";
  std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char *>(data.data()),
               static_cast<std::streamsize>(data.size()));
  stream.close();
  if (!stream || !MoveFileExW(temporary.c_str(), path.c_str(),
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Could not write a save file. Check folder permissions.");
}
} // namespace

Session::Session(const std::filesystem::path &rom, const std::filesystem::path &firmware,
                 Audio &audio, const std::filesystem::path &ipl,
                 const std::filesystem::path &disk) {
  auto cartridge = rom.empty() ? std::vector<std::uint8_t>{} : read(rom, 0x0fc00000);
  auto pif = read(firmware, 0x7c0);
  n64::ConsoleConfig config{n64::VideoRegion::Ntsc, true, n64::CicModel::N6102,
                            rom.empty() ? 0u : 512u};
  config.disk_drive = !ipl.empty();
  console_ = std::make_unique<n64::Console>(config);
  if (config.disk_drive) {
    if (!console_->disk_drive().load_ipl(read(ipl, 0x400000)))
      throw std::runtime_error("Select a valid 64DD IPL firmware file.");
    clock_path_ = ipl;
    clock_path_.replace_extension(L".rtc");
    if (std::filesystem::exists(clock_path_) &&
        !console_->disk_drive().clock().load(read(clock_path_, 16)))
      throw std::runtime_error("The disk clock save must contain 16 bytes.");
    const auto clock = console_->disk_drive().clock().save();
    saved_clock_.assign(clock.begin(), clock.end());
    if (!disk.empty()) {
      if (!console_->disk_drive().load_image(read(disk, 0x435b0c0)))
        throw std::runtime_error("Select a valid 64DD disk image.");
      disk_path_ = disk;
      disk_path_.replace_extension(L".disk");
      if (std::filesystem::exists(disk_path_)) {
        const auto errors = console_->disk_drive().disk_errors();
        if (!console_->disk_drive().insert(read(disk_path_, 0x435b0c0), errors))
          throw std::runtime_error("The disk save has an invalid size.");
      }
      const auto data = console_->disk_drive().disk_data();
      saved_disk_.assign(data.begin(), data.end());
    }
  }
  if (!console_->load(cartridge, pif))
    throw std::runtime_error("Select a game or 64DD IPL and a 1984-byte NTSC PIF firmware file.");
  if (!rom.empty()) {
    save_path_ = rom;
    save_path_.replace_extension(L".eep");
  }
  auto memory = console_->eeprom().data();
  if (!save_path_.empty() && std::filesystem::exists(save_path_)) {
    auto data = read(save_path_, memory.size());
    if (data.size() != memory.size())
      throw std::runtime_error("The EEPROM save must contain 512 bytes for this SM64 profile.");
    std::copy(data.begin(), data.end(), memory.begin());
  }
  saved_.assign(memory.begin(), memory.end());
  console_->connect_controller(0, true);
  renderer_ = std::make_unique<n64::HardwareRenderer>(console_->ram());
  console_->audio().connect([&audio](n64::StereoSample sample) { audio.sample(sample); },
                            [&audio](unsigned rate) { audio.frequency(rate); });
  console_->display().connect(
      [this](std::span<const std::uint32_t> words) { renderer_->submit(words); },
      [this] { renderer_->synchronize(); },
      [this] {
        if (renderer_->crashed())
          console_->display().crash();
      });
  console_->video().connect_registers(
      [this](unsigned index, std::uint32_t value) { renderer_->write_video(index, value); });
  console_->video().connect_frame([this](bool field) {
    auto next = renderer_->frame(field);
    if (!next.rgba.empty())
      frame = std::move(next);
    ++frames;
  });
}

void Session::run(std::uint16_t buttons, std::int8_t x, std::int8_t y) {
  console_->controller(0).input(buttons, x, y);
  console_->run_interval();
  if (console_->frozen() || renderer_->crashed() ||
      console_->pif().state() == n64::Pif::State::Error)
    throw std::runtime_error(
        "Emulation stopped. Check the game and firmware files, then use Reset.");
}

void Session::save() {
  auto data = console_->eeprom().data();
  if (!save_path_.empty() && !std::equal(data.begin(), data.end(), saved_.begin())) {
    write_save(save_path_, data);
    saved_.assign(data.begin(), data.end());
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
