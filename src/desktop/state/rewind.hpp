#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace cupid::desktop {

class RewindHistory {
public:
  struct Settings {
    unsigned interval = 60;
    unsigned capacity = 10;
    std::size_t memory = 256 * 1024 * 1024;
    bool operator==(const Settings &) const = default;
  };
  using Capture = std::function<std::vector<std::uint8_t>()>;
  using Restore = std::function<void(std::span<const std::uint8_t>)>;
  RewindHistory(Capture capture, Restore restore, Settings settings);
  void configure(Settings settings);
  void clear();
  bool record(std::uint64_t frame);
  bool can_rewind(std::uint64_t frame) const;
  bool rewind(std::uint64_t frame);
  std::size_t size() const;
  std::size_t memory() const;

private:
  struct Snapshot {
    std::uint64_t frame;
    std::vector<std::uint8_t> bytes;
  };
  std::optional<std::size_t> previous(std::uint64_t frame) const;
  void remove_front();
  Capture capture_;
  Restore restore_;
  Settings settings_;
  std::deque<Snapshot> history_;
  std::optional<std::uint64_t> recorded_;
  std::size_t memory_ = 0;
};

} // namespace cupid::desktop
