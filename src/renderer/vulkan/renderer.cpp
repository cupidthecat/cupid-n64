#include "renderer/vulkan/renderer.hpp"
#include "rdp_device.hpp"
#include <atomic>
#include <cstring>
#include <stdexcept>

namespace cupid::n64 {

struct HardwareRenderer::Implementation : RDP::ValidationInterface {
  Rdram &ram;
  Vulkan::Context context;
  Vulkan::Device device;
  std::unique_ptr<RDP::CommandProcessor> processor;
  RDP::VIScanoutBuffer scanout;
  std::atomic<bool> crashed = false;

  explicit Implementation(Rdram &memory) : ram(memory) {
    if (!Vulkan::Context::init_loader(nullptr) ||
        !context.init_instance_and_device(nullptr, 0, nullptr, 0, 0))
      throw std::runtime_error("Vulkan device initialization failed");
    device.set_context(context);
    device.init_frame_contexts(3);
    processor = std::make_unique<RDP::CommandProcessor>(
        device, ram.words().data(), 0, ram.size(), ram.size() / 2,
        RDP::COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT);
    if (!processor->device_is_supported())
      throw std::runtime_error("Vulkan device does not support display rendering");
    processor->set_validation_interface(this);
    ram.bind_hidden(
        {static_cast<std::uint8_t *>(processor->begin_read_hidden_rdram()), ram.size() / 2});
  }

  ~Implementation() {
    if (processor) {
      processor->idle();
      ram.bind_hidden({});
    }
  }

  void report_rdp_crash(RDP::ValidationError, const char *) override {
    crashed.store(true, std::memory_order_relaxed);
  }
};

HardwareRenderer::HardwareRenderer(Rdram &ram)
    : implementation_(std::make_unique<Implementation>(ram)) {}

HardwareRenderer::~HardwareRenderer() = default;

void HardwareRenderer::submit(std::span<const std::uint32_t> words) {
  implementation_->processor->enqueue_command(static_cast<unsigned>(words.size()), words.data());
}

void HardwareRenderer::synchronize() {
  auto &processor = *implementation_->processor;
  processor.wait_for_timeline(processor.signal_timeline());
}

void HardwareRenderer::write_video(unsigned index, std::uint32_t value) {
  implementation_->processor->set_vi_register(static_cast<RDP::VIRegister>(index), value);
}

bool HardwareRenderer::crashed() const {
  return implementation_->crashed.load(std::memory_order_relaxed);
}

VideoFrame HardwareRenderer::frame(bool field) {
  auto &state = *implementation_;
  state.processor->set_vi_register(RDP::VIRegister::VCurrentLine, unsigned(field));
  RDP::ScanoutOptions options;
  options.persist_frame_on_invalid_input = true;
  options.upscale_deinterlacing = true;
  if (state.scanout.fence)
    state.scanout.fence->wait();
  state.processor->scanout_async_buffer(state.scanout, options);
  state.processor->begin_frame_context();
  VideoFrame frame;
  if (!state.scanout.fence || !state.scanout.width || !state.scanout.height)
    return frame;
  state.scanout.fence->wait();
  frame.width = state.scanout.width;
  frame.height = state.scanout.height;
  frame.rgba.resize(std::size_t(frame.width) * frame.height * 4);
  const auto *pixels =
      state.device.map_host_buffer(*state.scanout.buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
  std::memcpy(frame.rgba.data(), pixels, frame.rgba.size());
  state.device.unmap_host_buffer(*state.scanout.buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
  return frame;
}

} // namespace cupid::n64
