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
} // namespace

Session::Session(const std::filesystem::path &rom, const std::filesystem::path &firmware,
                 Audio &audio) {
  auto cartridge = read(rom, 0x0fc00000);
  auto pif = read(firmware, 0x7c0);
  console_ = std::make_unique<n64::Console>(
      n64::ConsoleConfig{n64::VideoRegion::Ntsc, true, n64::CicModel::N6102, 512});
  if (!console_->load(cartridge, pif))
    throw std::runtime_error("Select an N64 cartridge and a 1984-byte NTSC PIF firmware file.");
  save_path_ = rom;
  save_path_.replace_extension(L".eep");
  auto memory = console_->eeprom().data();
  if (std::filesystem::exists(save_path_)) {
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
  if (console_->frozen() || renderer_->crashed())
    throw std::runtime_error("Emulation stopped. Use Reset to restart the cartridge.");
}

void Session::save() {
  auto data = console_->eeprom().data();
  if (std::equal(data.begin(), data.end(), saved_.begin()))
    return;
  auto temporary = save_path_;
  temporary += L".tmp";
  std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char *>(data.data()),
               static_cast<std::streamsize>(data.size()));
  stream.close();
  if (!stream || !MoveFileExW(temporary.c_str(), save_path_.c_str(),
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error(
        "Could not write the EEPROM save beside the ROM. Check folder permissions.");
  saved_.assign(data.begin(), data.end());
}

} // namespace cupid::desktop
