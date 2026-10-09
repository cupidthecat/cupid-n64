#include "desktop/video/output.hpp"
#if defined(CUPID_HARDWARE_RENDERER)
#include "renderer/vulkan/renderer.hpp"
#endif
#include <stdexcept>
#include <utility>

namespace cupid::desktop {

struct VideoOutput::Implementation {
  n64::Console &console;
  n64::VideoFrame software_frame;
  bool requires_hardware = false;
  std::function<void(bool)> begin;
  std::function<n64::VideoFrame()> read;
  std::function<bool()> crashed;
#if defined(CUPID_HARDWARE_RENDERER)
  std::unique_ptr<n64::HardwareRenderer> hardware;
#endif

  Implementation(n64::Console &console, bool hardware_rendering) : console(console) {
#if defined(CUPID_HARDWARE_RENDERER)
    if (hardware_rendering) {
      try {
        hardware = std::make_unique<n64::HardwareRenderer>(console.ram());
      } catch (const std::runtime_error &) {
      }
    }
    if (hardware) {
      console.display().connect(
          [this](std::span<const std::uint32_t> words) { hardware->submit(words); },
          [this] { hardware->synchronize(); },
          [this] {
            if (hardware->crashed())
              this->console.display().crash();
          });
      console.video().connect_registers(
          [this](unsigned index, std::uint32_t value) { hardware->write_video(index, value); });
      begin = [this](bool field) { hardware->begin_frame(field); };
      read = [this] { return hardware->read_frame(); };
      crashed = [this] { return hardware->crashed(); };
      return;
    }
#else
    (void)hardware_rendering;
#endif
    console.display().connect(
        [this](std::span<const std::uint32_t> words) {
          const auto code = (words.front() >> 24) & 63;
          if ((code >= 8 && code <= 15) || code == 0x24 || code == 0x25 || code == 0x36)
            requires_hardware = true;
        },
        {});
    console.video().connect_registers({});
  }

  ~Implementation() {
    console.display().connect({}, {});
    console.video().connect_registers({});
  }
};

VideoOutput::VideoOutput(n64::Console &console, bool hardware_rendering)
    : implementation_(std::make_unique<Implementation>(console, hardware_rendering)) {}

VideoOutput::~VideoOutput() = default;

void VideoOutput::begin_frame(bool field) {
  auto &state = *implementation_;
  if (state.begin)
    state.begin(field);
  else
    state.software_frame = state.console.video().scanout(state.console.ram());
}

n64::VideoFrame VideoOutput::read_frame() {
  auto &state = *implementation_;
  return state.read ? state.read() : std::move(state.software_frame);
}

bool VideoOutput::requires_hardware() const {
  return implementation_->requires_hardware;
}

bool VideoOutput::crashed() const {
  const auto &state = *implementation_;
  return state.crashed && state.crashed();
}

bool VideoOutput::hardware_rendering() const {
  return bool(implementation_->begin);
}

} // namespace cupid::desktop
