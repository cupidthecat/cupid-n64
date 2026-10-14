#include "desktop/audio/sdl/output.hpp"
#include "desktop/linux/audio/devices.hpp"
#include "desktop/linux/text.hpp"

namespace cupid::desktop::linux_host {

std::vector<std::string> audio_devices() {
  std::vector<std::string> result;
  int count = 0;
  const auto devices = SDL_GetAudioPlaybackDevices(&count);
  for (int n = 0; n < count; ++n)
    if (const auto name = SDL_GetAudioDeviceName(devices[n]))
      result.emplace_back(name);
  SDL_free(devices);
  return result;
}

} // namespace cupid::desktop::linux_host

namespace cupid::desktop {
namespace {

std::wstring wide(const char *text) {
  return text ? linux_host::wide(text) : std::wstring{};
}

class LinuxAudioOutput final : public SdlAudioOutput {
public:
  LinuxAudioOutput() : SdlAudioOutput(wide, true) {
    open();
  }

private:
  Endpoint endpoint() const override {
    return named_endpoint(settings().device);
  }
};

} // namespace

std::unique_ptr<AudioOutput> make_audio_output(std::span<short *const>) {
  return std::make_unique<LinuxAudioOutput>();
}

} // namespace cupid::desktop
