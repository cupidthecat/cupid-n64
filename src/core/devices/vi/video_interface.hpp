#pragma once

#include "core/devices/mi/mips_interface.hpp"
#include "core/devices/vi/frame.hpp"
#include "core/timing/frequencies.hpp"
#include <functional>

namespace cupid::n64 {

class VideoInterface {
public:
  explicit VideoInterface(MipsInterface &interrupts, VideoRegion region = VideoRegion::Ntsc);
  void power();
  void advance(std::uint32_t clocks);
  void elapse(std::uint32_t clocks) {
    clock_ -= clocks;
  }
  void run();
  std::uint32_t read_word(std::uint32_t address) const;
  void write_word(std::uint32_t address, std::uint32_t value);
  void connect_frame(std::function<void(bool)> callback);
  void connect_registers(std::function<void(unsigned, std::uint32_t)> callback);
  void connect_sync(std::function<void()> callback);
  const VideoFrame &scanout(Rdram &ram);
  bool active() const {
    return (registers_[0] & 3) != 0;
  }
  bool field() const {
    return field_;
  }
  std::uint64_t frames() const {
    return frames_;
  }
  std::int64_t clocks() const {
    return clock_;
  }
  std::uint32_t fraction() const {
    return fraction_;
  }

private:
  void step(std::uint32_t clocks);
  void line();
  MipsInterface &interrupts_;
  VideoRegion region_;
  std::array<std::uint32_t, 14> registers_{};
  std::uint32_t counter_ = 0;
  unsigned leap_counter_ = 0;
  bool field_ = false;
  std::uint32_t fraction_ = 0;
  std::int64_t clock_ = 0;
  std::uint64_t frames_ = 0;
  std::function<void(bool)> frame_;
  std::function<void(unsigned, std::uint32_t)> write_register_;
  std::function<void()> sync_;
  VideoFrame software_frame_;
};

} // namespace cupid::n64
