#pragma once

#include "core/cartridge/profile/profile.hpp"
#include "core/controller/accessories/cartridge/handheld.hpp"
#include "core/debug/inspection.hpp"
#include "core/system/console.hpp"
#include "desktop/controls/feedback.hpp"
#include "desktop/controls/input.hpp"
#include "desktop/controls/mouse/motion.hpp"
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
  void run(const PlayerInputs &input);
  void run();
  void advance_frame(const PlayerInputs &input);
  void input(const PlayerInputs &input);
  void clear_mouse_input();
  void graphics_settings(n64::RenderSettings settings);
  n64::RenderSettings graphics_settings() const {
    return settings_.graphics;
  }
  bool hardware_rendering() const {
    return output_->hardware_rendering();
  }
  const PortStatus &port_status(unsigned port) const {
    return ports_.at(port);
  }
  const std::array<PortStatus, 4> &automatic_ports() const {
    return automatic_ports_;
  }
  FeedbackState feedback_state(unsigned port) const;
  void heart_rate(unsigned port, unsigned value);
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
  std::vector<n64::MemoryRegion> memory_regions() const;
  std::vector<std::uint8_t> read_memory(n64::MemoryRegionId region, std::uint32_t offset,
                                        unsigned length);
  void write_memory(n64::MemoryRegionId region, std::uint32_t offset,
                    std::span<const std::uint8_t> bytes);
  std::vector<n64::DeviceProperty> device_properties(n64::DeviceId device) const;
  std::uint64_t clocks() const {
    return console_->cpu().state().clocks;
  }
  unsigned frames = 0;
  n64::VideoFrame frame;

private:
  friend class SessionState;
  void configure_ports(const std::array<n64::ControllerAccessory, 4> &defaults,
                       const std::filesystem::path &save_image);
  void attach_transfer(unsigned port, const std::filesystem::path &path);
  void save_transfers();
  void advance_to_frame();
  void legacy_input(std::uint16_t buttons, std::int8_t x, std::int8_t y);
  void run_interval(bool frame_boundary);
  Audio &audio_;
  SessionSettings settings_;
  std::array<PortStatus, 4> ports_;
  std::array<PortStatus, 4> automatic_ports_;
  std::array<MouseMotion, 4> mouse_motion_;
  struct TransferMedia {
    std::shared_ptr<n64::HandheldCartridge> cartridge;
    std::filesystem::path clock_path;
    std::vector<std::uint8_t> saved_clock;
  };
  std::array<TransferMedia, 4> transfer_media_;
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
