#pragma once

#include "core/system/console.hpp"
#include "desktop/storage/save.hpp"
#include "desktop/windows/audio.hpp"
#include "renderer/vulkan/renderer.hpp"
#include <filesystem>
#include <memory>

namespace cupid::desktop {

class Session {
public:
  Session(const std::filesystem::path &rom, const std::filesystem::path &firmware, Audio &audio,
          const std::filesystem::path &ipl = {}, const std::filesystem::path &disk = {},
          n64::ArcadeProfile arcade = n64::ArcadeProfile::Disabled);
  void run(std::uint16_t buttons, std::int8_t x, std::int8_t y);
  void reset();
  void save();
  std::uint64_t clocks() const {
    return console_->cpu().state().clocks;
  }
  unsigned frames = 0;
  n64::VideoFrame frame;

private:
  Audio &audio_;
  std::vector<MemorySave> saves_;
  std::filesystem::path cartridge_clock_path_;
  std::vector<std::uint8_t> saved_cartridge_clock_;
  std::filesystem::path clock_path_, disk_path_;
  std::vector<std::uint8_t> saved_clock_, saved_disk_;
  std::unique_ptr<n64::Console> console_;
  std::unique_ptr<n64::HardwareRenderer> renderer_;
};

} // namespace cupid::desktop
