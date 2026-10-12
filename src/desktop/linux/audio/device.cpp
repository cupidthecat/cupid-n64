#include "desktop/linux/audio/device.hpp"
#include "desktop/audio/resampler.hpp"
#include <algorithm>
#include <cstring>
#include <mutex>

namespace cupid::desktop::linux_host {
namespace {

class DeviceLock {
public:
  explicit DeviceLock(SDL_AudioDeviceID device) : device_(device) {
    if (device_)
      SDL_LockAudioDevice(device_);
  }
  ~DeviceLock() {
    if (device_)
      SDL_UnlockAudioDevice(device_);
  }

private:
  SDL_AudioDeviceID device_;
};

} // namespace

std::shared_ptr<AudioDevice> AudioDevice::open(const std::string &name) {
  static std::mutex mutex;
  static std::vector<std::weak_ptr<AudioDevice>> devices;
  const std::lock_guard lock(mutex);
  std::erase_if(devices, [](const auto &device) { return device.expired(); });
  for (const auto &weak : devices)
    if (const auto device = weak.lock(); device && device->name_ == name) {
      if (device->playing())
        return device;
      device->close();
    }
  std::shared_ptr<AudioDevice> device(new AudioDevice(name));
  if (!device->available())
    return {};
  devices.emplace_back(device);
  return device;
}

AudioDevice::AudioDevice(std::string name) : name_(std::move(name)) {
  SDL_AudioSpec format{};
  format.freq = AudioResampler::output_frequency;
  format.format = AUDIO_S16SYS;
  format.channels = 2;
  format.samples = 512;
  format.userdata = this;
  format.callback = [](void *data, Uint8 *output, int bytes) {
    if (bytes > 0)
      static_cast<AudioDevice *>(data)->mix(output, static_cast<unsigned>(bytes));
  };
  device_ = SDL_OpenAudioDevice(name_.empty() ? nullptr : name_.c_str(), 0, &format, nullptr, 0);
  if (device_)
    SDL_PauseAudioDevice(device_, 0);
}

AudioDevice::~AudioDevice() {
  close();
}

void AudioDevice::close() {
  if (device_)
    SDL_CloseAudioDevice(device_);
  device_ = 0;
}

void AudioDevice::attach(AudioQueue &queue) {
  const DeviceLock lock(device_);
  queues_.push_back(&queue);
}

void AudioDevice::detach(AudioQueue &queue) {
  const DeviceLock lock(device_);
  std::erase(queues_, &queue);
}

void AudioDevice::clear(AudioQueue &queue) {
  const DeviceLock lock(device_);
  queue.begin = queue.size = 0;
}

unsigned AudioDevice::queued(AudioQueue &queue) const {
  const DeviceLock lock(device_);
  return queue.size * sizeof(short);
}

bool AudioDevice::submit(AudioQueue &queue, std::span<const short> samples) {
  const DeviceLock lock(device_);
  if (!available() || samples.size() > queue.samples.size() - queue.size)
    return false;
  const auto offset = (queue.begin + queue.size) % queue.samples.size();
  const auto first = std::min(samples.size(), queue.samples.size() - offset);
  std::copy_n(samples.begin(), first, queue.samples.begin() + offset);
  std::copy(samples.begin() + first, samples.end(), queue.samples.begin());
  queue.size += static_cast<unsigned>(samples.size());
  return true;
}

bool AudioDevice::playing() const {
  return device_ && SDL_GetAudioDeviceStatus(device_) == SDL_AUDIO_PLAYING;
}

void AudioDevice::mix(Uint8 *output, unsigned bytes) {
  started_.store(true, std::memory_order_relaxed);
  std::memset(output, 0, bytes);
  for (auto queue : queues_) {
    unsigned offset = 0;
    while (queue->size && offset < bytes) {
      const auto samples = std::min({queue->size, (bytes - offset) / unsigned(sizeof(short)),
                                     static_cast<unsigned>(queue->samples.size()) - queue->begin});
      if (!samples)
        break;
      SDL_MixAudioFormat(output + offset,
                         reinterpret_cast<const Uint8 *>(queue->samples.data() + queue->begin),
                         AUDIO_S16SYS, samples * sizeof(short), SDL_MIX_MAXVOLUME);
      offset += samples * sizeof(short);
      queue->size -= samples;
      queue->begin = (queue->begin + samples) % queue->samples.size();
    }
  }
}

} // namespace cupid::desktop::linux_host
