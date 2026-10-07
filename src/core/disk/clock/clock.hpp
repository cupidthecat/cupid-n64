#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace cupid::n64 {

class DiskClock {
public:
  using HostClock = std::function<std::int64_t()>;
  explicit DiskClock(HostClock clock = {});
  bool load(std::span<const std::uint8_t> data);
  std::array<std::uint8_t, 16> save() const;
  std::uint16_t read(unsigned pair) const;
  void write(unsigned pair, std::uint16_t value);
  void tick();
  bool valid() const;

private:
  void initialize();
  void advance(std::uint64_t seconds);
  HostClock clock_;
  std::array<std::uint8_t, 16> data_{};
};

} // namespace cupid::n64
