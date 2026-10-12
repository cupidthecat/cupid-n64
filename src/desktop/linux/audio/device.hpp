#pragma once

#include <SDL.h>
#include <array>
#include <atomic>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace cupid::desktop::linux_host {

struct AudioQueue {
  std::array<short, 32 * 960> samples{};
  unsigned begin = 0, size = 0;
};

class AudioDevice {
public:
  static std::shared_ptr<AudioDevice> open(const std::string &name);
  ~AudioDevice();
  void attach(AudioQueue &queue);
  void detach(AudioQueue &queue);
  void clear(AudioQueue &queue);
  unsigned queued(AudioQueue &queue) const;
  bool submit(AudioQueue &queue, std::span<const short> samples);
  bool available() const {
    return device_ != 0;
  }
  bool playing() const;
  bool started() const {
    return started_.load(std::memory_order_relaxed);
  }

private:
  explicit AudioDevice(std::string name);
  void close();
  void mix(Uint8 *output, unsigned bytes);
  std::string name_;
  SDL_AudioDeviceID device_ = 0;
  std::vector<AudioQueue *> queues_;
  std::atomic<bool> started_ = false;
};

} // namespace cupid::desktop::linux_host
