#include "desktop/audio.hpp"
#include "desktop/audio/output.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace cupid::desktop {

Audio::Audio() {
  std::array<short *, 32> rendered{};
  for (unsigned n = 0; n < rendered.size(); ++n)
    rendered[n] = buffers_[n].rendered.data();
  output_ = make_audio_output(rendered);
}

Audio::~Audio() = default;

bool Audio::available() const {
  return output_->available();
}

const std::wstring &Audio::device_name() const {
  return output_->device_name();
}

bool Audio::using_fallback() const {
  return output_->using_fallback();
}

std::wstring Audio::error() const {
  return output_->error();
}

void Audio::configure(Settings settings) {
  if (settings.levels.volume > 200 || settings.levels.balance < -100 ||
      settings.levels.balance > 100 || settings.latency < 20 || settings.latency > 320 ||
      settings.latency % 10)
    throw std::invalid_argument("Invalid audio volume, balance, or latency.");
  if (settings.device != settings_.device || settings.latency != settings_.latency)
    clear();
  settings_ = std::move(settings);
  output_->configure(settings_);
}

bool Audio::recover() {
  if (!output_->recover())
    return false;
  buffer_ = offset_ = 0;
  resampler_.clear();
  return true;
}

bool Audio::recovery_pending() const {
  return output_->recovery_pending();
}

std::vector<Audio::Device> Audio::devices() const {
  return output_->devices();
}

void Audio::frequency(unsigned value) {
  resampler_.frequency(value);
}

void Audio::clear() {
  output_->clear();
  buffer_ = offset_ = 0;
  resampler_.clear();
}

void Audio::sample(n64::StereoSample value) {
  if (measure_)
    ++generated_samples_;
  if (!available() || suspended)
    return;
  resampler_.write(value, [this](n64::StereoSample sample) {
    if (!available() || (!offset_ && !wait()))
      return;
    auto &buffer = buffers_[buffer_];
    const auto convert = [this](double value) {
      return muted ? short(0)
                   : static_cast<short>(std::clamp(value, -1.0, 32767.0 / 32768) * 32768);
    };
    buffer.samples[offset_++] = convert(sample.left);
    buffer.samples[offset_++] = convert(sample.right);
    if (offset_ == buffer.samples.size())
      queue(offset_);
  });
}

void Audio::play_saved_output() {
  if (!available() || muted || suspended || !offset_ || !wait())
    return;
  queue(offset_);
}

void Audio::queue(unsigned samples) {
  auto &buffer = buffers_[buffer_];
  render_audio(std::span(buffer.samples).first(samples), std::span(buffer.rendered).first(samples),
               settings_.levels);
  output_->submit(buffer_, std::span(buffer.rendered).first(samples));
  offset_ = 0;
  buffer_ = (buffer_ + 1) % (settings_.latency / 10);
}

bool Audio::wait() {
  if (!measure_)
    return output_->wait(buffer_);
  const auto started = std::chrono::steady_clock::now();
  const auto ready = output_->wait(buffer_);
  wait_time_ += std::chrono::steady_clock::now() - started;
  return ready;
}

} // namespace cupid::desktop
