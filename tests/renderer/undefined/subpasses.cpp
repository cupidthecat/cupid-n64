#include "../../support/test.hpp"
#include "context.hpp"
#include "device.hpp"
#include "render_pass.hpp"
#include <array>
#include <stdexcept>

namespace test {

void gpu_subpass_attachment_tests() {
  Vulkan::Context context;
  if (!Vulkan::Context::init_loader(nullptr) ||
      !context.init_instance_and_device(nullptr, 0, nullptr, 0, 0))
    throw std::runtime_error("Vulkan device initialization failed");
  Vulkan::Device device;
  device.set_context(context);
  device.init_frame_contexts(1);
  for (unsigned colors : {0u, 1u, 2u})
    for (unsigned inputs : {0u, 1u, 2u}) {
      std::array<VkAttachmentDescription2, 5> attachments{};
      for (unsigned n = 0; n < attachments.size(); ++n) {
        auto &attachment = attachments[n];
        attachment.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
        attachment.format = n == 4 ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.finalLayout = n == 4 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                        : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      }
      std::array<VkAttachmentReference2, 2> color{}, input{};
      for (unsigned n = 0; n < color.size(); ++n) {
        color[n].sType = input[n].sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
        color[n].attachment = n;
        color[n].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        input[n].attachment = n + 2;
        input[n].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      }
      VkAttachmentReference2 depth{};
      depth.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
      depth.attachment = colors ? VK_ATTACHMENT_UNUSED : 4;
      depth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      VkSubpassDescription2 subpass{};
      subpass.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
      subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpass.colorAttachmentCount = colors;
      subpass.pColorAttachments = colors ? color.data() : nullptr;
      subpass.inputAttachmentCount = inputs;
      subpass.pInputAttachments = inputs ? input.data() : nullptr;
      subpass.pDepthStencilAttachment = &depth;
      VkRenderPassCreateInfo2 info{};
      info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
      info.attachmentCount = static_cast<unsigned>(attachments.size());
      info.pAttachments = attachments.data();
      info.subpassCount = 1;
      info.pSubpasses = &subpass;
      Vulkan::RenderPass pass(0, &device, info);
      equal(pass.get_render_pass() != VK_NULL_HANDLE, true);
      equal(pass.get_num_subpasses(), 1);
      equal(pass.get_sample_count(0), 1);
      equal(pass.get_num_color_attachments(0), colors);
      equal(pass.get_num_input_attachments(0), inputs);
      for (unsigned n = 0; n < colors; ++n) {
        equal(pass.get_color_attachment(0, n).attachment, n);
        equal(pass.get_color_attachment(0, n).layout, color[n].layout);
      }
      for (unsigned n = 0; n < inputs; ++n) {
        equal(pass.get_input_attachment(0, n).attachment, n + 2);
        equal(pass.get_input_attachment(0, n).layout, input[n].layout);
      }
    }
}

} // namespace test
