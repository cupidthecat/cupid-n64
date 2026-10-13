#include "renderer/vulkan/implementation.hpp"
#include "thread_id.hpp"
#include <cstring>
#include <stdexcept>

namespace cupid::n64 {

HardwareRenderer::Implementation::Implementation(Rdram &memory, RenderSettings selected,
                                                 std::filesystem::path cache_file)
    : ram(memory), settings(selected), pipeline_cache(std::move(cache_file)) {
  if (!settings.valid())
    throw std::runtime_error("Select a supported rendering configuration.");
  if (!Vulkan::Context::init_loader(nullptr) ||
      !context.init_instance_and_device(nullptr, 0, nullptr, 0, 0))
    throw std::runtime_error("Vulkan device initialization failed");
  // The command queue, serialized caller, and readback worker use distinct pools.
  context.set_num_thread_indices(3);
  device.set_context(context);
  const auto cached = pipeline_cache.read();
  if (!device.init_pipeline_cache(cached.data(), cached.size()))
    device.init_pipeline_cache(nullptr, 0);
  Util::register_thread_index(1);
  device.init_frame_contexts(3);
  RDP::CommandProcessorFlags flags = RDP::COMMAND_PROCESSOR_FLAG_HOST_VISIBLE_HIDDEN_RDRAM_BIT;
  if (settings.scale == 2)
    flags |= RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_2X_BIT;
  if (settings.scale == 4)
    flags |= RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_4X_BIT;
  if (settings.scale == 8)
    flags |= RDP::COMMAND_PROCESSOR_FLAG_UPSCALING_8X_BIT;
  if (settings.scale > 1)
    flags |= RDP::COMMAND_PROCESSOR_FLAG_SUPER_SAMPLED_DITHER_BIT;
  processor = std::make_unique<RDP::CommandProcessor>(device, ram.data_.data(), 0, ram.size(),
                                                      ram.size() / 2, flags);
  if (!processor->device_is_supported())
    throw std::runtime_error("Vulkan device does not support display rendering");
  processor->set_validation_interface(this);
  readback = std::make_unique<FrameReadback>([this] { return read_pixels(); });
  // A new processor supplies the power-on coverage bits.
  ram.hidden_view_ = {static_cast<std::uint8_t *>(processor->begin_read_hidden_rdram()),
                      ram.size() / 2};
}

HardwareRenderer::Implementation::~Implementation() {
  Util::register_thread_index(1);
  readback.reset();
  if (processor) {
    processor->idle();
    ram.bind_hidden({});
    processor.reset();
    if (pipeline_cache.enabled()) {
      try {
        const auto size = device.get_pipeline_cache_size();
        if (size && size <= PipelineCacheFile::maximum_bytes) {
          std::vector<std::uint8_t> bytes(size);
          if (device.get_pipeline_cache_data(bytes.data(), bytes.size()))
            pipeline_cache.write(bytes);
        }
      } catch (...) {
      }
    }
  }
}

VideoFrame HardwareRenderer::Implementation::read_pixels() {
  Util::register_thread_index(2);
  if (!scanout.fence || !scanout.width || !scanout.height)
    return {1, 1, {0, 0, 0, 255}};
  scanout.fence->wait();
  VideoFrame frame;
  frame.width = scanout.width;
  frame.height = scanout.height;
  frame.rgba.resize(std::size_t(frame.width) * frame.height * 4);
  const auto *pixels = device.map_host_buffer(*scanout.buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
  std::memcpy(frame.rgba.data(), pixels, frame.rgba.size());
  device.unmap_host_buffer(*scanout.buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
  return frame;
}

void HardwareRenderer::Implementation::report_rdp_crash(RDP::ValidationError, const char *) {
  crashed.store(true, std::memory_order_relaxed);
}

HardwareRenderer::HardwareRenderer(Rdram &ram, RenderSettings settings,
                                   std::filesystem::path pipeline_cache)
    : implementation_(std::make_unique<Implementation>(ram, settings, std::move(pipeline_cache))) {}

HardwareRenderer::~HardwareRenderer() = default;

void HardwareRenderer::bind_thread() const {
  // Callers transfer ownership only after the previous execution interval completes.
  Util::register_thread_index(1);
}

void HardwareRenderer::submit(std::span<const std::uint32_t> words) {
  bind_thread();
  implementation_->processor->enqueue_command(static_cast<unsigned>(words.size()), words.data());
}

void HardwareRenderer::synchronize() {
  bind_thread();
  auto &processor = *implementation_->processor;
  processor.wait_for_timeline(processor.signal_timeline());
}

void HardwareRenderer::write_video(unsigned index, std::uint32_t value) {
  bind_thread();
  implementation_->processor->set_vi_register(static_cast<RDP::VIRegister>(index), value);
}

bool HardwareRenderer::crashed() const {
  return implementation_->crashed.load(std::memory_order_relaxed);
}

RenderSettings HardwareRenderer::settings() const {
  return implementation_->settings;
}

void HardwareRenderer::scanout_settings(RenderSettings settings) {
  bind_thread();
  if (!settings.valid())
    throw std::runtime_error("Select a supported rendering configuration.");
  if (settings.scale != implementation_->settings.scale)
    throw std::runtime_error("Reload the game to change internal rendering resolution.");
  implementation_->readback->wait_presented();
  implementation_->settings = settings;
}

void HardwareRenderer::connect_present(std::function<void(unsigned, const VideoFrame &)> present) {
  bind_thread();
  implementation_->readback->connect_present(std::move(present));
}

void HardwareRenderer::wait_presented() {
  implementation_->readback->wait_presented();
}

void HardwareRenderer::begin_frame(bool field, unsigned number) {
  bind_thread();
  auto &state = *implementation_;
  state.readback->wait();
  state.processor->set_vi_register(RDP::VIRegister::VCurrentLine, unsigned(field));
  RDP::ScanoutOptions options;
  options.persist_frame_on_invalid_input = true;
  options.downscale_steps = state.settings.supersampling ? 16 : 0;
  options.blend_previous_frame = state.settings.weave;
  options.upscale_deinterlacing = !state.settings.weave;
  if (state.settings.bypass_vi)
    options.vi = {false, false, true, false, false, false};
  if (state.scanout.fence)
    state.scanout.fence->wait();
  state.processor->scanout_async_buffer(state.scanout, options);
  state.processor->begin_frame_context();
  state.readback->start(number);
}

VideoFrame HardwareRenderer::read_frame() {
  bind_thread();
  auto &state = *implementation_;
  return state.readback->take();
}

VideoFrame HardwareRenderer::frame(bool field) {
  begin_frame(field);
  return read_frame();
}

} // namespace cupid::n64
