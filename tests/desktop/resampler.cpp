#include "desktop/audio/resampler.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>

namespace {
using cupid::desktop::AudioResampler;
using cupid::n64::StereoSample;
unsigned checks = 0, failures = 0;
void check(bool value) {
  ++checks;
  failures += !value;
}
void near(double actual, double expected) {
  check(std::isfinite(actual) && std::abs(actual - expected) < 1e-12);
}
std::vector<StereoSample> pulse(AudioResampler &resampler, unsigned count = 128) {
  std::vector<StereoSample> output;
  for (unsigned index = 0; index < count; ++index)
    resampler.write({index == 0 ? 1.0 : 0.0, index == 0 ? -0.5 : 0.0},
                    [&](StereoSample sample) { output.push_back(sample); });
  return output;
}
double signal_rms(double frequency) {
  AudioResampler resampler;
  resampler.frequency(192000);
  unsigned count = 0, measured = 0;
  double sum = 0;
  for (unsigned index = 0; index < 192000; ++index) {
    const auto value = 0.75 * std::sin(2 * std::numbers::pi * frequency * index / 192000);
    resampler.write({value, 0}, [&](StereoSample sample) {
      if (++count >= 4096) {
        sum += sample.left * sample.left;
        ++measured;
      }
      near(sample.right, 0);
    });
  }
  return std::sqrt(sum / measured);
}
} // namespace

int main() {
  AudioResampler impulse;
  impulse.frequency(32000);
  const auto output = pulse(impulse, 5);
  constexpr double expected[]{0, -4.0 / 27, 11.0 / 27, 1, 11.0 / 27, -4.0 / 27, 0, 0};
  check(output.size() == std::size(expected));
  for (unsigned index = 0; index < output.size() && index < std::size(expected); ++index) {
    near(output[index].left, expected[index]);
    near(output[index].right, expected[index] * -0.5);
  }
  for (unsigned rate : {743u, 11025u, 32000u, 32006u, 44100u, 48000u, 96000u, 192000u}) {
    AudioResampler resampler;
    resampler.frequency(rate);
    unsigned count = 0;
    for (unsigned index = 0; index < rate; ++index) {
      const auto value = std::sin(index * 0.13);
      resampler.write({value, -value}, [&](StereoSample sample) {
        ++count;
        near(sample.left + sample.right, 0);
      });
    }
    check(count >= 48000 && count <= 48001);
    resampler.clear();
    AudioResampler fresh;
    fresh.frequency(rate);
    auto reset = pulse(resampler), initial = pulse(fresh);
    check(reset.size() == initial.size());
    for (unsigned index = 0; index < reset.size() && index < initial.size(); ++index) {
      near(reset[index].left, initial[index].left);
      near(reset[index].right, initial[index].right);
    }
    for (unsigned next : {32000u, 44100u, 96000u}) {
      resampler.frequency(next);
      fresh.frequency(next);
      reset = pulse(resampler);
      initial = pulse(fresh);
      check(reset.size() == initial.size());
      for (unsigned index = 0; index < reset.size() && index < initial.size(); ++index) {
        near(reset[index].left, initial[index].left);
        near(reset[index].right, initial[index].right);
      }
    }
  }
  const auto audible = signal_rms(1000), ultrasonic = signal_rms(40000);
  check(audible > 0.52 && audible < 0.54);
  check(ultrasonic < 0.02);
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
