#include "core/devices/audio/audio_interface.hpp"
#include <cmath>
#include <utility>

namespace cupid::n64 {

AudioInterface::AudioInterface(Rdram &ram, MipsInterface &interrupts, VideoRegion region)
    : ram_(ram), interrupts_(interrupts), region_(region) {
  power();
}

void AudioInterface::power() {
  state_ = {};
  output_ = {};
  clock_ = 0;
  frequency_ = 44100;
  precision_ = 16;
  period_ = clock_frequency / frequency_;
  update_decay();
}

void AudioInterface::update_decay() {
  decay_ = std::exp(-1.0 / (frequency_ * 0.003));
}

void AudioInterface::connect(std::function<void(StereoSample)> sample,
                             std::function<void(unsigned)> rate) {
  sample_ = std::move(sample);
  rate_ = std::move(rate);
  if (rate_)
    rate_(frequency_);
}

void AudioInterface::connect_sync(std::function<void()> callback) {
  sync_ = std::move(callback);
}

StereoSample AudioInterface::sample() {
  bool active = false;
  if (state_.count && state_.lengths[0] && state_.enable) {
    state_.addresses[0] = (state_.addresses[0] + unsigned(state_.carry) * 0x2000) & 0xffffff;
    const auto data = static_cast<std::uint32_t>(ram_.read(state_.addresses[0], 4));
    output_.left = static_cast<std::int16_t>(data >> 16) / 32768.0;
    output_.right = static_cast<std::int16_t>(data) / 32768.0;
    state_.addresses[0] = (state_.addresses[0] & ~0x1fffu) | ((state_.addresses[0] + 4) & 0x1fff);
    state_.carry = (state_.addresses[0] & 0x1fff) == 0;
    state_.lengths[0] = (state_.lengths[0] - 4) & 0x3ffff;
    active = true;
  }
  if (state_.count && state_.lengths[0] == 0) {
    if (--state_.count) {
      state_.addresses[0] = state_.addresses[1];
      state_.lengths[0] = state_.lengths[1];
      interrupts_.raise(Interrupt::Audio);
    }
  }
  if (!active) {
    output_.left *= decay_;
    output_.right *= decay_;
    if (std::abs(output_.left) < 1e-7)
      output_.left = 0;
    if (std::abs(output_.right) < 1e-7)
      output_.right = 0;
  }
  return output_;
}

void AudioInterface::advance(std::uint32_t clocks) {
  elapse(clocks);
  run();
}

void AudioInterface::run() {
  while (clock_ < 0) {
    const auto value = sample();
    if (sample_)
      sample_(value);
    clock_ += period_;
  }
}

} // namespace cupid::n64
