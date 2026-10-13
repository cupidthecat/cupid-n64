#include "desktop/audio/sdl/output.hpp"
#include <algorithm>
#include <limits>
#include <utility>

namespace cupid::desktop {

SdlAudioOutput::SdlAudioOutput(Encoder wide, bool poll_devices)
    : wide_(wide), poll_devices_(poll_devices) {
  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
  initialized_ = SDL_InitSubSystem(SDL_INIT_AUDIO);
  if (!initialized_)
    fail();
}

SdlAudioOutput::~SdlAudioOutput() {
  close();
  if (initialized_)
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SdlAudioOutput::configure(const Audio::Settings &settings) {
  const bool changed = settings.device != settings_.device || settings.latency != settings_.latency;
  settings_ = settings;
  if (changed || !available())
    open();
}

bool SdlAudioOutput::recover() {
  if (initialized_) {
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_AUDIO_DEVICE_ADDED, SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED);
  }
  const bool changed = changed_->exchange(false);
  if (!poll_devices_ && !changed && available())
    return false;
  const auto desired = endpoint();
  if (available() && desired.device == requested_ && device_present() &&
      (!changed || selected_ == requested_)) {
    fallback_ = desired.fallback || selected_ != requested_;
    name_ = wide_(SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(stream_)));
    return false;
  }
  open();
  return true;
}

bool SdlAudioOutput::recovery_pending() const {
  return changed_->load() || !available();
}

void SdlAudioOutput::probe() {
  if (initialized_) {
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_AUDIO_DEVICE_ADDED, SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED);
  }
  if (!available()) {
    changed_->store(true);
    return;
  }
  const auto desired = endpoint();
  if (desired.device != requested_ || !device_present() ||
      (desired.fallback || selected_ != requested_) != fallback_ ||
      wide_(SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(stream_))) != name_)
    changed_->store(true);
}

std::vector<Audio::Device> SdlAudioOutput::devices() const {
  std::vector<Audio::Device> result;
  int count = 0;
  const auto devices = SDL_GetAudioPlaybackDevices(&count);
  for (int n = 0; n < count; ++n)
    if (const auto name = SDL_GetAudioDeviceName(devices[n])) {
      auto converted = wide_(name);
      result.push_back({converted, std::move(converted)});
    }
  SDL_free(devices);
  return result;
}

void SdlAudioOutput::clear() {
  if (stream_ && !SDL_ClearAudioStream(stream_))
    fail();
}

bool SdlAudioOutput::wait(unsigned) {
  if (!available())
    return false;
  const auto limit = static_cast<int>((AudioResampler::output_frequency / 100) * sizeof(float) * 2 *
                                      (settings_.latency / 10));
  std::uint64_t progress = 0;
  auto timeout = std::uint64_t{5000000000};
  int previous = std::numeric_limits<int>::max();
  for (;;) {
    if (interrupt_ && interrupt_())
      return false;
    const auto queued = SDL_GetAudioStreamQueued(stream_);
    if (queued < 0) {
      fail();
      return false;
    }
    if (queued <= limit)
      return true;
    const auto now = SDL_GetTicksNS();
    if (!progress)
      progress = now;
    if (previous != std::numeric_limits<int>::max() && queued < previous) {
      progress = now;
      timeout = 1000000000;
    }
    previous = queued;
    if (now - progress >= timeout) {
      fail(L"The audio device stopped accepting samples.");
      return false;
    }
    const auto frames = static_cast<std::uint64_t>(queued - limit) / (sizeof(float) * 2);
    const auto delay =
        std::max<std::uint64_t>(1000, frames * 1000000000 / AudioResampler::output_frequency);
    SDL_DelayNS(std::min<std::uint64_t>(delay, 1000000));
    if (!SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(stream_))) {
      fail();
      return false;
    }
  }
}

void SdlAudioOutput::submit(unsigned, std::span<const short> samples) {
  std::vector<float> converted;
  converted.reserve(samples.size());
  for (const auto sample : samples)
    converted.push_back(sample / 32768.0f);
  submit_float(converted);
}

bool SdlAudioOutput::floating_samples() const {
  return true;
}

void SdlAudioOutput::submit_float(std::span<const float> samples) {
  if (!available() || samples.empty())
    return;
  if (samples.size() % 2 ||
      samples.size_bytes() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Invalid stereo audio buffer size.");
  if (!SDL_PutAudioStreamData(stream_, samples.data(), static_cast<int>(samples.size_bytes())))
    fail();
}

void SdlAudioOutput::wait_interrupt(std::function<bool()> predicate) {
  interrupt_ = std::move(predicate);
}

bool SdlAudioOutput::available() const {
  return opened_.load();
}

const std::wstring &SdlAudioOutput::device_name() const {
  return name_;
}

bool SdlAudioOutput::using_fallback() const {
  return available() && fallback_;
}

std::wstring SdlAudioOutput::error() const {
  std::lock_guard lock(error_mutex_);
  return error_;
}

const Audio::Settings &SdlAudioOutput::settings() const {
  return settings_;
}

SdlAudioOutput::Endpoint SdlAudioOutput::named_endpoint(std::wstring_view preferred) const {
  Endpoint selected;
  selected.fallback = !preferred.empty();
  if (preferred.empty())
    return selected;
  int count = 0;
  const auto devices = SDL_GetAudioPlaybackDevices(&count);
  for (int n = 0; n < count; ++n)
    if (wide_(SDL_GetAudioDeviceName(devices[n])) == preferred) {
      selected.device = devices[n];
      selected.fallback = false;
      break;
    }
  SDL_free(devices);
  return selected;
}

bool SdlAudioOutput::device_present() const {
  const auto name = SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(stream_));
  if (!name)
    return false;
  int count = 0;
  const auto devices = SDL_GetAudioPlaybackDevices(&count);
  bool present = false;
  for (int n = 0; n < count; ++n)
    if ((selected_ == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK &&
         wide_(SDL_GetAudioDeviceName(devices[n])) == wide_(name)) ||
        devices[n] == selected_) {
      present = true;
      break;
    }
  SDL_free(devices);
  return present;
}

void SdlAudioOutput::fail(std::wstring message) {
  opened_ = false;
  changed_->store(true);
  if (message.empty())
    message = wide_(SDL_GetError());
  if (message.empty())
    message = L"The audio device stopped accepting samples.";
  std::lock_guard lock(error_mutex_);
  error_ = std::move(message);
}

void SdlAudioOutput::open() {
  close();
  if (!initialized_) {
    initialized_ = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!initialized_) {
      fail();
      return;
    }
  }
  SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "480");
  SDL_AudioSpec format{SDL_AUDIO_F32, 2, AudioResampler::output_frequency};
  const auto desired = endpoint();
  requested_ = selected_ = desired.device;
  fallback_ = desired.fallback;
  stream_ = SDL_OpenAudioDeviceStream(selected_, &format, nullptr, nullptr);
  if (!stream_ && selected_ != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK) {
    selected_ = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    fallback_ = true;
    stream_ = SDL_OpenAudioDeviceStream(selected_, &format, nullptr, nullptr);
  }
  if (!stream_ || !SDL_ResumeAudioStreamDevice(stream_)) {
    fail();
    return;
  }
  if (!device_present()) {
    fail(L"No playback device is available.");
    return;
  }
  name_ = wide_(SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(stream_)));
  {
    std::lock_guard lock(error_mutex_);
    error_.clear();
  }
  changed_->store(false);
  opened_ = true;
}

void SdlAudioOutput::close() {
  opened_ = false;
  if (stream_)
    SDL_DestroyAudioStream(std::exchange(stream_, nullptr));
}

} // namespace cupid::desktop
