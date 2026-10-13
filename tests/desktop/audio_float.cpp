#include "../support/test.hpp"
#include "desktop/audio/output.hpp"
#include <cmath>

namespace cupid::desktop {
namespace {
struct Capture {
  std::vector<float> samples;
  unsigned integer_submissions = 0;
  bool interrupted = false;
} capture;

class Output final : public AudioOutput {
public:
  void configure(const Audio::Settings &) override {}
  bool recover() override {
    return false;
  }
  void clear() override {}
  bool wait(unsigned) override {
    return !capture.interrupted;
  }
  void submit(unsigned, std::span<const short>) override {
    ++capture.integer_submissions;
  }
  bool floating_samples() const override {
    return true;
  }
  void submit_float(std::span<const float> samples) override {
    test::equal(samples.size(), std::size_t(2));
    capture.samples.insert(capture.samples.end(), samples.begin(), samples.end());
  }
  bool available() const override {
    return true;
  }
  const std::wstring &device_name() const override {
    static const std::wstring name = L"Capture";
    return name;
  }
  bool using_fallback() const override {
    return false;
  }
  std::wstring error() const override {
    return {};
  }
};
} // namespace

std::unique_ptr<AudioOutput> make_audio_output(std::span<short *const>) {
  return std::make_unique<Output>();
}
} // namespace cupid::desktop

int main() {
  using namespace cupid::desktop;
  Audio audio;
  test::equal(audio.settings().latency, 40u);
  audio.frequency(48000);
  audio.measure(true);
  for (unsigned n = 0; n < 8; ++n)
    audio.sample({1e-8, -1e-8});
  test::equal(capture.integer_submissions, 0u);
  test::equal(capture.samples.empty(), false);
  if (capture.samples.size() >= 2) {
    test::equal(std::abs(capture.samples[capture.samples.size() - 2] - 1e-8f) < 1e-12f, true);
    test::equal(std::abs(capture.samples.back() + 1e-8f) < 1e-12f, true);
  }
  test::equal(audio.generated_samples(), std::uint64_t(8));

  const auto before = capture.samples.size();
  capture.interrupted = true;
  audio.sample({0.5, 0.5});
  test::equal(capture.samples.size(), before);
  test::equal(audio.available(), true);
  capture.interrupted = false;
  audio.sample({0.5, 0.5});
  test::equal(capture.samples.size() > before, true);
  audio.muted = true;
  audio.sample({0.5, 0.5});
  test::equal(capture.samples.empty(), false);
  if (!capture.samples.empty())
    test::equal(capture.samples.back(), 0.0f);
  audio.suspended = true;
  const auto suspended = capture.samples.size();
  audio.sample({0.5, 0.5});
  test::equal(capture.samples.size(), suspended);

  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
