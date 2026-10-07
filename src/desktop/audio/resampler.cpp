#include "desktop/audio/resampler.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace cupid::desktop {

void AudioResampler::frequency(unsigned rate) {
  rate = std::max(1u, rate);
  ratio_ = double(rate) / output_frequency;
  filter_count_ = rate >= output_frequency * 2 ? 3 : 0;
  if (filter_count_) {
    const auto cutoff = std::min(25000.0, output_frequency / 2.0 - 2000);
    const auto tangent = std::tan(std::numbers::pi * cutoff / rate);
    const auto squared = tangent * tangent;
    for (unsigned stage = 0; stage < filter_count_; ++stage) {
      const auto quality = 0.5 / std::cos(std::numbers::pi * (stage + 0.5) / 6);
      const auto damping = tangent / quality;
      const auto scale = 1 / (1 + damping + squared);
      auto &filter = filters_[stage];
      filter.direct = squared * scale;
      filter.middle = 2 * filter.direct;
      filter.feedback1 = 2 * (squared - 1) * scale;
      filter.feedback2 = (1 - damping + squared) * scale;
    }
  }
  clear();
}

void AudioResampler::clear() {
  phase_ = 0;
  history_ = {};
  for (auto &filter : filters_)
    filter.delay1 = filter.delay2 = {};
}

n64::StereoSample AudioResampler::LowPass::process(n64::StereoSample input) {
  const auto channel = [&](double value, double &first, double &second) {
    const auto output = value * direct + first;
    first = value * middle + second - output * feedback1;
    second = value * direct - output * feedback2;
    return output;
  };
  return {channel(input.left, delay1.left, delay2.left),
          channel(input.right, delay1.right, delay2.right)};
}

n64::StereoSample AudioResampler::interpolate() const {
  const auto channel = [&](double first, double second, double third, double fourth) {
    const auto cubic = fourth - third - first + second;
    const auto quadratic = first - second - cubic;
    const auto linear = third - first;
    return ((cubic * phase_ + quadratic) * phase_ + linear) * phase_ + second;
  };
  return {channel(history_[0].left, history_[1].left, history_[2].left, history_[3].left),
          channel(history_[0].right, history_[1].right, history_[2].right, history_[3].right)};
}

} // namespace cupid::desktop
