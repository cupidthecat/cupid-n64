#include "renderer/state/renderer_state.hpp"
#include "renderer/vulkan/implementation.hpp"
#include <cstring>
#include <utility>

namespace cupid::n64 {
namespace {

Vulkan::BufferHandle staging(Vulkan::Device &device, std::size_t size) {
  Vulkan::BufferCreateInfo info;
  info.domain = Vulkan::BufferDomain::CachedHost;
  info.size = size;
  info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  auto buffer = device.create_buffer(info);
  if (!buffer)
    throw std::runtime_error("Snapshot staging allocation failed");
  return buffer;
}

std::vector<std::uint8_t> read_buffer(Vulkan::Device &device, Vulkan::Buffer &source) {
  auto buffer = staging(device, static_cast<std::size_t>(source.get_create_info().size));
  auto command = device.request_command_buffer(Vulkan::CommandBuffer::Type::AsyncCompute);
  command->barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
  command->copy_buffer(*buffer, source);
  command->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
  Vulkan::Fence fence;
  device.submit(command, &fence);
  fence->wait();
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(source.get_create_info().size));
  const auto *mapped = device.map_host_buffer(*buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
  std::memcpy(bytes.data(), mapped, bytes.size());
  device.unmap_host_buffer(*buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
  return bytes;
}

std::vector<std::uint8_t> read_image(Vulkan::Device &device, Vulkan::Image &image,
                                     VkImageLayout layout) {
  const auto bytes = std::size_t(image.get_width()) * image.get_height() * 4;
  auto buffer = staging(device, bytes);
  auto command = device.request_command_buffer(Vulkan::CommandBuffer::Type::AsyncCompute);
  command->image_barrier(image, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
  command->copy_image_to_buffer(*buffer, image, 0, {}, {image.get_width(), image.get_height(), 1},
                                0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
  command->image_barrier(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layout,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT);
  command->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
  Vulkan::Fence fence;
  device.submit(command, &fence);
  fence->wait();
  std::vector<std::uint8_t> result(bytes);
  const auto *mapped = device.map_host_buffer(*buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
  std::memcpy(result.data(), mapped, bytes);
  device.unmap_host_buffer(*buffer, Vulkan::MEMORY_ACCESS_READ_BIT);
  return result;
}

} // namespace

void RendererState::require_memory(HardwareRenderer &renderer, Rdram &memory) {
  state::Archive::require(&renderer.implementation_->ram == &memory);
}

void RendererState::fence(HardwareRenderer &renderer) {
  auto &s = *renderer.implementation_;
  s.readback->wait();
  s.processor->idle();
  if (s.scanout.fence)
    s.scanout.fence->wait();
}

std::vector<std::uint8_t> RendererState::capture(HardwareRenderer &renderer) {
  fence(renderer);
  state::Archive archive;
  visit(archive, renderer);
  return archive.finish();
}

StateCheckpoint RendererState::checkpoint(HardwareRenderer &renderer) {
  fence(renderer);
  state::Archive archive(true);
  visit(archive, renderer);
  auto ranges = archive.ranges();
  auto bytes = archive.finish();
  return {std::move(bytes), std::move(ranges)};
}

std::unique_ptr<state::Archive> RendererState::prepare(HardwareRenderer &renderer,
                                                       std::span<const std::uint8_t> data) {
  auto archive = std::make_unique<state::Archive>(data);
  fence(renderer);
  archive->defer([&renderer] { renderer.implementation_->processor->renderer.reset_context(); });
  visit(*archive, renderer);
  archive->validate();
  return archive;
}

void RendererState::visit(state::Archive &a, HardwareRenderer &renderer) {
  auto &s = *renderer.implementation_;
  auto &p = *s.processor;
  a.label("gpu.identity");
  a.identity<std::uint64_t>(0x4554415453445052ull);
  a.identity<std::uint32_t>(1);
  a.identity<std::uint64_t>(0x1cecd042b2619bc5ull);
  a.identity(static_cast<std::uint32_t>(p.rdram_size));
  registers(a, p);
  a.label("gpu.crashed");
  const auto crashed = a.literal(s.crashed.load(std::memory_order_relaxed));
  a.defer([&s, crashed] { s.crashed.store(crashed, std::memory_order_relaxed); });
  auto tmem = std::make_shared<std::vector<std::uint8_t>>();
  if (!a.loading())
    *tmem = read_buffer(s.device, *p.tmem);
  *tmem = a.owned_vector(*tmem, 4096, "gpu.tmem");
  state::Archive::require(tmem->size() == 4096);

  auto &v = p.vi;
  a.label("gpu.previous_image.present");
  const bool has_image = a.literal(bool(v.prev_scanout_image));
  auto pixels = std::make_shared<std::vector<std::uint8_t>>();
  std::uint32_t width = 0, height = 0, format = VK_FORMAT_R8G8B8A8_UNORM;
  if (!a.loading() && has_image) {
    width = v.prev_scanout_image->get_width();
    height = v.prev_scanout_image->get_height();
    format = v.prev_scanout_image->get_format();
    state::Archive::require(!v.prev_image_is_external);
    state::Archive::require(format == VK_FORMAT_R8G8B8A8_UNORM);
    *pixels = read_image(s.device, *v.prev_scanout_image, v.prev_image_layout);
  }
  a.label("gpu.previous_image.width", 4);
  width = a.literal(width);
  a.label("gpu.previous_image.height", 4);
  height = a.literal(height);
  a.label("gpu.previous_image.format", 4);
  format = a.literal(format);
  state::Archive::require(width <= 4096 && height <= 4096);
  state::Archive::require(format == VK_FORMAT_R8G8B8A8_UNORM);
  *pixels = a.owned_vector(*pixels, 4096 * 4096 * 4, "gpu.previous_image.rgba");
  state::Archive::require(pixels->size() == std::uint64_t(width) * height * 4);
  state::Archive::require(has_image ? width && height : !width && !height);
  auto restored_readback = std::make_shared<VideoFrame>();
  if (!a.loading())
    *restored_readback = s.readback->capture();
  a.label("gpu.readback.width", 4);
  restored_readback->width = a.literal(restored_readback->width);
  a.label("gpu.readback.height", 4);
  restored_readback->height = a.literal(restored_readback->height);
  state::Archive::require(restored_readback->width <= 4096 && restored_readback->height <= 4096);
  state::Archive::require(bool(restored_readback->width) == bool(restored_readback->height));
  restored_readback->rgba =
      a.owned_vector(restored_readback->rgba, 4096 * 4096 * 4, "gpu.readback.rgba");
  state::Archive::require(restored_readback->rgba.size() ==
                          std::uint64_t(restored_readback->width) * restored_readback->height * 4);
  if (a.loading()) {
    a.validate();
    auto texture_upload = staging(s.device, tmem->size());
    auto *mapped = s.device.map_host_buffer(*texture_upload, Vulkan::MEMORY_ACCESS_WRITE_BIT);
    std::memcpy(mapped, tmem->data(), tmem->size());
    s.device.unmap_host_buffer(*texture_upload, Vulkan::MEMORY_ACCESS_WRITE_BIT);
    Vulkan::ImageHandle previous;
    if (has_image) {
      Vulkan::ImageCreateInfo info =
          Vulkan::ImageCreateInfo::render_target(width, height, VK_FORMAT_R8G8B8A8_UNORM);
      info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      Vulkan::ImageInitialData initial{pixels->data(), 0, 0};
      previous = s.device.create_image(info, &initial);
      if (!previous)
        throw std::runtime_error("Snapshot image allocation failed");
    }
    Vulkan::BufferHandle memory_upload;
    if (!p.is_host_coherent)
      memory_upload = staging(s.device, p.rdram_size);
    auto command = s.device.request_command_buffer(Vulkan::CommandBuffer::Type::AsyncCompute);
    command->barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                     VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    command->copy_buffer(*p.tmem, *texture_upload);
    if (memory_upload) {
      command->copy_buffer(*p.rdram, p.rdram_offset, *memory_upload, 0, p.rdram_size);
      command->fill_buffer(*p.rdram, 0, p.rdram_offset + p.rdram_size, p.rdram_size);
    }
    command->barrier(VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                     VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT,
                     VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                     VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    a.defer([&s, &p, texture_upload, memory_upload, command, previous,
             restored_readback]() mutable {
      if (memory_upload) {
        auto *mapped = s.device.map_host_buffer(*memory_upload, Vulkan::MEMORY_ACCESS_WRITE_BIT);
        std::memcpy(mapped, std::as_const(s.ram).words().data(), p.rdram_size);
        s.device.unmap_host_buffer(*memory_upload, Vulkan::MEMORY_ACCESS_WRITE_BIT);
        auto &coherency = p.renderer.incoherent;
        for (auto *pages : {&coherency.page_to_direct_copy, &coherency.page_to_masked_copy,
                            &coherency.page_to_pending_readback})
          std::fill(pages->begin(), pages->end(), 0);
        for (unsigned page = 0; page < coherency.num_pages; ++page)
          coherency.pending_writes_for_page[page].store(0, std::memory_order_relaxed);
        coherency.staging_readback_index = 0;
      } else {
        p.end_write_rdram();
      }
      p.end_write_hidden_rdram();
      Vulkan::Fence fence;
      s.device.submit(command, &fence);
      fence->wait();
      p.vi.prev_scanout_image = previous;
      p.vi.prev_image_layout =
          previous ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
      p.vi.prev_image_is_external = false;
      s.scanout = {};
      s.readback->restore(std::move(*restored_readback));
    });
  }
}

} // namespace cupid::n64
