#include "desktop/audio/levels.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cupid::desktop {

void render_audio(std::span<const std::int16_t> samples, std::span<std::int16_t> output,
                  AudioLevels levels) {
  if (samples.size() != output.size() || samples.size() % 2 || levels.volume > 200 ||
      levels.balance < -100 || levels.balance > 100)
    throw std::invalid_argument("Invalid stereo audio levels or buffer size.");
  if (levels == AudioLevels{}) {
    std::copy(samples.begin(), samples.end(), output.begin());
    return;
  }
  const double volume = levels.volume / 100.0;
  const double left = volume * (1.0 - std::max(levels.balance, 0) / 100.0);
  const double right = volume * (1.0 + std::min(levels.balance, 0) / 100.0);
  const auto convert = [](std::int16_t sample, double gain) {
    return static_cast<std::int16_t>(std::clamp(std::round(sample * gain), -32768.0, 32767.0));
  };
  for (std::size_t n = 0; n < samples.size(); n += 2) {
    output[n] = convert(samples[n], left);
    output[n + 1] = convert(samples[n + 1], right);
  }
}

} // namespace cupid::desktop
