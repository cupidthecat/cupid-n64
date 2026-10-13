#pragma once

#include "desktop/audio.hpp"
#include <functional>
#include <span>
#include <stdexcept>

namespace cupid::desktop {

class AudioOutput {
public:
  virtual ~AudioOutput() = default;
  virtual void configure(const Audio::Settings &settings) = 0;
  virtual bool recover() = 0;
  virtual void probe() {}
  virtual bool recovery_pending() const {
    return !available();
  }
  virtual std::vector<Audio::Device> devices() const {
    return {};
  }
  virtual void clear() = 0;
  virtual bool wait(unsigned buffer) = 0;
  virtual void submit(unsigned buffer, std::span<const short> samples) = 0;
  virtual bool floating_samples() const {
    return false;
  }
  virtual void submit_float(std::span<const float>) {
    throw std::logic_error("The audio output does not accept floating point samples.");
  }
  virtual void wait_interrupt(std::function<bool()>) {}
  virtual bool available() const = 0;
  virtual const std::wstring &device_name() const = 0;
  virtual bool using_fallback() const = 0;
  virtual std::wstring error() const = 0;
};

std::unique_ptr<AudioOutput> make_audio_output(std::span<short *const> buffers);

} // namespace cupid::desktop
