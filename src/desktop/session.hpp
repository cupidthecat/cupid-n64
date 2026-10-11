#pragma once

#include "core/system/console.hpp"
#include "desktop/session/settings.hpp"
#include "desktop/storage/save.hpp"
#include "desktop/video/output.hpp"
#include "desktop/windows/audio.hpp"
#include <filesystem>
#include <memory>

namespace cupid::desktop {

class Session {
public:
  Session(const std::filesystem::path &rom, const std::filesystem::path &firmware, Audio &audio,
          const std::filesystem::path &ipl = {}, const std::filesystem::path &disk = {},
          n64::ArcadeProfile arcade = n64::ArcadeProfile::Disabled,
          std::optional<std::uint64_t> random_seed = {}, bool hardware_rendering = true,
          SessionSettings settings = {});
  void run(std::uint16_t buttons, std::int8_t x, std::int8_t y);
  void advance_frame(std::uint16_t buttons, std::int8_t x, std::int8_t y);
  void finish_frame();
  void reset();
  void save();
  void change_disk(const std::filesystem::path &path);
  void eject_disk();
  bool has_disk_drive() const;
  const std::filesystem::path &disk_image() const {
    return disk_image_;
  }
  const std::filesystem::path &storage_image() const {
    return storage_image_;
  }
  std::vector<std::uint8_t> capture_state();
  void restore_state(std::span<const std::uint8_t> bytes);
  std::uint64_t clocks() const {
    return console_->cpu().state().clocks;
  }
  unsigned frames = 0;
  n64::VideoFrame frame;

private:
  friend class SessionState;
  void run_interval(std::uint16_t buttons, std::int8_t x, std::int8_t y, bool frame_boundary);
  Audio &audio_;
  SessionSettings settings_;
  std::filesystem::path disk_image_, storage_image_;
  std::filesystem::path cartridge_image_, ipl_image_;
  std::uint64_t ipl_identity_ = 0;
  std::vector<MemorySave> saves_;
  std::filesystem::path cartridge_clock_path_;
  std::vector<std::uint8_t> saved_cartridge_clock_;
  std::filesystem::path clock_path_, disk_path_;
  std::vector<std::uint8_t> saved_clock_, saved_disk_;
  std::unique_ptr<n64::Console> console_;
  std::unique_ptr<VideoOutput> output_;
  bool hardware_rendering_ = true;
  bool frame_pending_ = false;
  std::uint64_t disk_identity_ = 0;
};

} // namespace cupid::desktop
