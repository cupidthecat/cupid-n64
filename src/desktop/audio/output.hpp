#pragma once

#include "desktop/audio.hpp"
#include <span>

namespace cupid::desktop {

class AudioOutput {
public:
  virtual ~AudioOutput() = default;
  virtual void configure(const Audio::Settings &settings) = 0;
  virtual bool recover() = 0;
  virtual void clear() = 0;
  virtual bool wait(unsigned buffer) = 0;
  virtual void submit(unsigned buffer, std::span<const short> samples) = 0;
  virtual bool available() const = 0;
  virtual const std::wstring &device_name() const = 0;
  virtual bool using_fallback() const = 0;
  virtual std::wstring error() const = 0;
};

std::unique_ptr<AudioOutput> make_audio_output(std::span<short *const> buffers);

} // namespace cupid::desktop
