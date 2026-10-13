#pragma once

#include "desktop/audio/output.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <mutex>
#include <string_view>

namespace cupid::desktop {

// Stream changes and teardown require a stopped producer. Probe runs on the main thread.
class SdlAudioOutput : public AudioOutput {
public:
  ~SdlAudioOutput() override;
  void configure(const Audio::Settings &settings) override;
  bool recover() override;
  void probe() override;
  bool recovery_pending() const override;
  std::vector<Audio::Device> devices() const override;
  void clear() override;
  bool wait(unsigned buffer) override;
  void submit(unsigned buffer, std::span<const short> samples) override;
  bool floating_samples() const override;
  void submit_float(std::span<const float> samples) override;
  void wait_interrupt(std::function<bool()> predicate) override;
  bool available() const override;
  const std::wstring &device_name() const override;
  bool using_fallback() const override;
  std::wstring error() const override;

protected:
  using Encoder = std::wstring (*)(const char *);
  struct Endpoint {
    SDL_AudioDeviceID device = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    bool fallback = false;
  };
  explicit SdlAudioOutput(Encoder wide, bool poll_devices = false);
  virtual Endpoint endpoint() const = 0;
  void open();
  void close();
  Endpoint named_endpoint(std::wstring_view preferred) const;
  const Audio::Settings &settings() const;
  std::shared_ptr<std::atomic<bool>> changed_ = std::make_shared<std::atomic<bool>>(false);

private:
  bool device_present() const;
  void fail(std::wstring message = {});
  Encoder wide_;
  SDL_AudioStream *stream_ = nullptr;
  SDL_AudioDeviceID selected_ = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
  SDL_AudioDeviceID requested_ = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
  Audio::Settings settings_;
  std::function<bool()> interrupt_;
  std::atomic<bool> opened_{false};
  mutable std::mutex error_mutex_;
  std::wstring name_, error_;
  bool initialized_ = false, fallback_ = false, poll_devices_ = false;
};

} // namespace cupid::desktop
