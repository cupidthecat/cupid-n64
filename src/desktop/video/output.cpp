#include "desktop/video/output.hpp"
#include "core/state/core_state.hpp"
#if defined(CUPID_HARDWARE_RENDERER)
#include "renderer/state/machine_state.hpp"
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

  Implementation(n64::Console &console, bool hardware_rendering, n64::RenderSettings settings)
      : console(console) {
    if (!settings.valid())
      throw std::runtime_error("Select a supported rendering configuration.");
#if defined(CUPID_HARDWARE_RENDERER)
    if (hardware_rendering) {
      try {
        hardware = std::make_unique<n64::HardwareRenderer>(console.ram(), settings);
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
    if (settings != n64::RenderSettings{})
      throw std::runtime_error("These graphics settings require an available hardware renderer.");
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

VideoOutput::VideoOutput(n64::Console &console, bool hardware_rendering,
                         n64::RenderSettings settings)
    : implementation_(std::make_unique<Implementation>(console, hardware_rendering, settings)) {}

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
  return state.read ? state.read() : std::exchange(state.software_frame, {});
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

bool VideoOutput::supports_hardware() {
#if defined(CUPID_HARDWARE_RENDERER)
  return true;
#else
  return false;
#endif
}

void VideoOutput::scanout_settings(n64::RenderSettings settings) {
#if defined(CUPID_HARDWARE_RENDERER)
  if (implementation_->hardware) {
    implementation_->hardware->scanout_settings(settings);
    return;
  }
#endif
  if (settings != n64::RenderSettings{})
    throw std::runtime_error("These graphics settings require an available hardware renderer.");
}

void VideoOutput::synchronize() {
#if defined(CUPID_HARDWARE_RENDERER)
  if (implementation_->hardware)
    implementation_->hardware->synchronize();
#endif
}

std::vector<std::uint8_t> VideoOutput::capture_state() {
  auto &state = *implementation_;
  n64::state::Archive archive;
  archive.identity<std::uint64_t>(0x45544154534f5043ull);
  archive.identity<std::uint32_t>(1);
  archive.identity(hardware_rendering());
  archive.literal(state.requires_hardware);
  archive.literal(state.software_frame.width);
  archive.literal(state.software_frame.height);
  archive.owned_vector(state.software_frame.rgba, 640 * 576 * 4);
  std::vector<std::uint8_t> machine;
#if defined(CUPID_HARDWARE_RENDERER)
  if (state.hardware)
    machine = n64::MachineState::capture(state.console, *state.hardware);
  else
#endif
    machine = n64::CoreState::capture(state.console);
  archive.owned_vector(machine, 1856u * 1024 * 1024);
  return archive.finish();
}

void VideoOutput::restore_state(std::span<const std::uint8_t> bytes) {
  auto &state = *implementation_;
  n64::state::Archive archive(bytes);
  archive.identity<std::uint64_t>(0x45544154534f5043ull);
  archive.identity<std::uint32_t>(1);
  archive.identity(hardware_rendering());
  const auto requires_hardware = archive.literal(false);
  n64::VideoFrame frame;
  frame.width = archive.literal(0u);
  frame.height = archive.literal(0u);
  n64::state::Archive::require(frame.width <= 640 && frame.height <= 576);
  frame.rgba = archive.owned_vector<std::uint8_t>({}, 640 * 576 * 4);
  n64::state::Archive::require(frame.rgba.size() == std::uint64_t(frame.width) * frame.height * 4);
  const auto machine = archive.owned_vector<std::uint8_t>({}, 1856u * 1024 * 1024);
  archive.validate();
#if defined(CUPID_HARDWARE_RENDERER)
  if (state.hardware)
    n64::MachineState::restore(state.console, *state.hardware, machine);
  else
#endif
    n64::CoreState::restore(state.console, machine);
  state.requires_hardware = requires_hardware;
  state.software_frame = std::move(frame);
}

} // namespace cupid::desktop
