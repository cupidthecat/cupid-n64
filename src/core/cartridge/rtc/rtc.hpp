#pragma once

#include "core/devices/joybus/device.hpp"
#include "core/timing/event_queue.hpp"

namespace cupid::n64 {

class Rtc : public JoybusDevice {
public:
  using HostClock = std::function<std::int64_t()>;
  using Storage = std::array<std::uint8_t, 32>;
  Rtc(EventQueue &events, bool present = false, HostClock clock = {});
  void power();
  void tick();
  void advance(std::uint64_t seconds);
  bool load(std::span<const std::uint8_t> storage);
  Storage save();
  bool present() const {
    return present_;
  }
  bool running() const {
    return present_ && !(status_ & 0x80);
  }
  JoybusStatus communicate(std::span<const std::uint8_t> input,
                           std::span<std::uint8_t> output) override;

private:
  void run(bool enabled);
  void restore_time();
  EventQueue &events_;
  HostClock clock_;
  Storage data_{};
  std::uint8_t status_ = 0;
  std::uint8_t write_lock_ = 0;
  bool present_ = false;
};

} // namespace cupid::n64
