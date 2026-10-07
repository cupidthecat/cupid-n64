#pragma once

#include "core/devices/audio/audio_interface.hpp"

namespace cupid::desktop {

class AudioResampler {
public:
  static constexpr unsigned output_frequency = 48000;
  void frequency(unsigned rate);
  void clear();
  template <class Output> void write(n64::StereoSample sample, Output &&output) {
    sample.left += 1e-25;
    sample.right += 1e-25;
    for (unsigned stage = 0; stage < filter_count_; ++stage)
      sample = filters_[stage].process(sample);
    for (unsigned index = 0; index < 3; ++index)
      history_[index] = history_[index + 1];
    history_.back() = sample;
    while (phase_ <= 1.0) {
      output(interpolate());
      phase_ += ratio_;
    }
    phase_ -= 1.0;
  }

private:
  struct LowPass {
    double direct = 0, middle = 0, feedback1 = 0, feedback2 = 0;
    n64::StereoSample delay1, delay2;
    n64::StereoSample process(n64::StereoSample input);
  };
  n64::StereoSample interpolate() const;
  std::array<n64::StereoSample, 4> history_{};
  std::array<LowPass, 3> filters_{};
  unsigned filter_count_ = 0;
  double phase_ = 0, ratio_ = 44100.0 / output_frequency;
};

} // namespace cupid::desktop
