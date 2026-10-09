#include "core/devices/vi/video_interface.hpp"
#include <utility>

namespace cupid::n64 {

VideoInterface::VideoInterface(MipsInterface &interrupts, VideoRegion region)
    : interrupts_(interrupts), region_(region) {
  power();
}

void VideoInterface::power() {
  registers_.fill(0);
  registers_[3] = 256;
  counter_ = leap_counter_ = fraction_ = 0;
  field_ = false;
  clock_ = 0;
  frames_ = 0;
  software_frame_ = {};
}

void VideoInterface::connect_frame(std::function<void(bool)> callback) {
  frame_ = std::move(callback);
}

void VideoInterface::connect_registers(std::function<void(unsigned, std::uint32_t)> callback) {
  write_register_ = std::move(callback);
}

void VideoInterface::connect_sync(std::function<void()> callback) {
  sync_ = std::move(callback);
}

void VideoInterface::step(std::uint32_t clocks) {
  const auto scaled = std::uint64_t(clocks) * clock_frequency + fraction_;
  clock_ += static_cast<std::int64_t>(scaled / video_frequency(region_));
  fraction_ = static_cast<std::uint32_t>(scaled % video_frequency(region_));
}

void VideoInterface::advance(std::uint32_t clocks) {
  elapse(clocks);
  run();
}

void VideoInterface::run() {
  while (clock_ < 0)
    line();
}

void VideoInterface::line() {
  if (!active()) {
    counter_ = 0;
    step(0x800);
    return;
  }
  counter_ = (counter_ + 1) & 511;
  const auto total = registers_[6];
  if (((counter_ << 1) | unsigned(field_)) >= total + 1) {
    counter_ = 0;
    field_ ^= !(total & 1);
    if (++leap_counter_ == 5)
      leap_counter_ = 0;
  }
  if (counter_ == (registers_[10] >> 17)) {
    ++frames_;
    if (frame_)
      frame_(field_);
  }
  const auto coincidence = registers_[3];
  if ((total & 1) || (coincidence & 1)) {
    if (counter_ == (coincidence >> 1))
      interrupts_.raise(Interrupt::Video);
  } else {
    const auto half_line = (counter_ << 1) | unsigned(field_);
    if ((!field_ && half_line == coincidence) || (field_ && half_line + 1 == coincidence) ||
        (!field_ && half_line == total && coincidence == 0))
      interrupts_.raise(Interrupt::Video);
  }
  auto duration = (registers_[7] & 0xfff) + 1;
  if (counter_ == 1) {
    const auto leap = (registers_[7] >> (16 + leap_counter_)) & 1;
    duration = (registers_[8] >> (leap * 16)) & 0xfff;
  }
  step(duration);
}

} // namespace cupid::n64
