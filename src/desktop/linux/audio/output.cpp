#include "desktop/audio/output.hpp"
#include "desktop/linux/audio/device.hpp"
#include "desktop/linux/audio/devices.hpp"
#include "desktop/linux/text.hpp"
#include <algorithm>
#include <chrono>

namespace cupid::desktop::linux_host {

std::vector<std::string> audio_devices() {
  std::vector<std::string> devices;
  for (int n = 0; n < SDL_GetNumAudioDevices(0); ++n)
    if (const auto name = SDL_GetAudioDeviceName(n, 0))
      devices.emplace_back(name);
  return devices;
}

} // namespace cupid::desktop::linux_host

namespace cupid::desktop {
namespace {

class LinuxAudioOutput final : public AudioOutput {
public:
  LinuxAudioOutput() {
    open();
  }
  ~LinuxAudioOutput() override {
    close();
    if (initialized_)
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
  void configure(const Audio::Settings &settings) override {
    const bool changed = settings.device != settings_.device;
    settings_ = settings;
    if (changed || !available()) {
      close();
      open();
    }
  }
  bool recover() override {
    const auto devices = linux_host::audio_devices();
    const auto preferred = linux_host::utf8(settings_.device);
    const bool present = std::find(devices.begin(), devices.end(), preferred) != devices.end();
    const bool active_present =
        active_.empty() || std::find(devices.begin(), devices.end(), active_) != devices.end();
    const bool returned = present && !preferred_present_;
    preferred_present_ = present;
    if (available() && device_->playing() && active_present && !(using_fallback() && returned))
      return false;
    close();
    open();
    return true;
  }
  void clear() override {
    if (device_)
      device_->clear(queue_);
  }
  bool wait(unsigned) override {
    if (!available() || !device_->playing()) {
      error_ = L"The audio device stopped accepting samples.";
      return false;
    }
    const auto limit = (settings_.latency / 10 - 1) * 960 * sizeof(short);
    // Some backends prebuffer before their first playback callback.
    const auto timeout = device_->started() ? std::chrono::seconds(1) : std::chrono::seconds(5);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (available() && device_->queued(queue_) > limit) {
      if (std::chrono::steady_clock::now() >= deadline) {
        error_ = L"The audio device stopped accepting samples.";
        return false;
      }
      SDL_Delay(1);
    }
    return available();
  }
  void submit(unsigned, std::span<const short> samples) override {
    if (!device_->submit(queue_, samples))
      error_ = L"The audio device stopped accepting samples.";
  }
  bool available() const override {
    return device_ && device_->available() && error_.empty();
  }
  const std::wstring &device_name() const override {
    return name_;
  }
  bool using_fallback() const override {
    return available() && !settings_.device.empty() &&
           settings_.device != linux_host::wide(active_);
  }
  std::wstring error() const override {
    return error_;
  }

private:
  void open() {
    error_.clear();
    if (!initialized_) {
      SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
      initialized_ = SDL_InitSubSystem(SDL_INIT_AUDIO) == 0;
      if (!initialized_) {
        error_ = linux_host::wide(SDL_GetError());
        return;
      }
    }
    const auto devices = linux_host::audio_devices();
    const auto preferred = linux_host::utf8(settings_.device);
    preferred_present_ = std::find(devices.begin(), devices.end(), preferred) != devices.end();
    if (preferred_present_) {
      device_ = linux_host::AudioDevice::open(preferred);
      if (device_)
        active_ = preferred;
    }
    if (!device_)
      device_ = linux_host::AudioDevice::open({});
    if (!device_) {
      error_ = linux_host::wide(SDL_GetError());
      return;
    }
    device_->attach(queue_);
    name_ = active_.empty() ? L"System default" : linux_host::wide(active_);
  }
  void close() {
    if (device_)
      device_->detach(queue_);
    device_.reset();
    queue_.begin = queue_.size = 0;
    name_.clear();
    active_.clear();
  }
  std::shared_ptr<linux_host::AudioDevice> device_;
  linux_host::AudioQueue queue_;
  Audio::Settings settings_;
  std::wstring name_, error_;
  std::string active_;
  bool initialized_ = false, preferred_present_ = false;
};

} // namespace

std::unique_ptr<AudioOutput> make_audio_output(std::span<short *const>) {
  return std::make_unique<LinuxAudioOutput>();
}

} // namespace cupid::desktop
