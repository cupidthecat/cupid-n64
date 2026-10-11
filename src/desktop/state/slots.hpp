#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace cupid::desktop {

class StateSlots {
public:
  using Capture = std::function<std::vector<std::uint8_t>()>;
  using Restore = std::function<void(std::span<const std::uint8_t>)>;
  static constexpr unsigned count = 10;
  static constexpr std::size_t maximum_bytes = std::size_t(3072) * 1024 * 1024;
  StateSlots(const std::filesystem::path &image, Capture capture, Restore restore);
  void select(unsigned slot);
  unsigned selected() const;
  std::filesystem::path path(unsigned slot) const;
  bool occupied(unsigned slot) const;
  void save();
  void load();
  void undo_save();
  void undo_load();
  std::optional<unsigned> undo_save_slot() const;
  bool can_undo_load() const;

private:
  struct PreviousFile {
    unsigned slot = 0;
    bool existed = false;
    std::vector<std::uint8_t> bytes;
  };
  std::filesystem::path directory_;
  Capture capture_;
  Restore restore_;
  unsigned selected_ = 1;
  std::optional<PreviousFile> previous_file_;
  std::optional<std::vector<std::uint8_t>> previous_machine_;
};

} // namespace cupid::desktop
