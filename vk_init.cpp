#include "vk_init.hpp"

namespace vk {
namespace init {

VkPipelineShaderStageCreateInfo PipelineShaderStageCreateInfo(
    VkShaderStageFlagBits stage, VkShaderModule module) {
  VkPipelineShaderStageCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  info.pNext = nullptr;

  info.stage = stage;
  info.module = module;
  info.pName = "main";
  return info;
}

VkPipelineLayoutCreateInfo PipelineLayoutCreateInfo() {
  VkPipelineLayoutCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  info.pNext = nullptr;

  info.flags = 0;
  info.setLayoutCount = 0;
  info.pSetLayouts = nullptr;
  info.pushConstantRangeCount = 0;
  info.pPushConstantRanges = nullptr;

  return info;
}

VkPipelineVertexInputStateCreateInfo PipelineVertexInputStateCreateInfo() {
  VkPipelineVertexInputStateCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  info.pNext = nullptr;

  // No vertex bindings or attributes.
  info.vertexBindingDescriptionCount = 0;
  info.vertexAttributeDescriptionCount = 0;

  return info;
}

VkPipelineInputAssemblyStateCreateInfo PipelineInputAssemblyStateCreateInfo(
    VkPrimitiveTopology topology) {
  VkPipelineInputAssemblyStateCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  info.pNext = nullptr;

  info.topology = topology;

  info.primitiveRestartEnable = VK_FALSE;

  return info;
}

VkPipelineRasterizationStateCreateInfo PipelineRasterizationStateCreateInfo(
    VkPolygonMode polygon_mode) {
  VkPipelineRasterizationStateCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  info.pNext = nullptr;

  info.depthClampEnable = VK_FALSE;
  info.rasterizerDiscardEnable = VK_FALSE;

  info.polygonMode = polygon_mode;
  info.lineWidth = 1.f;

  // No backface culling.
  info.cullMode = VK_CULL_MODE_NONE;
  info.frontFace = VK_FRONT_FACE_CLOCKWISE;

  // No depth bias.
  info.depthBiasEnable = VK_FALSE;
  info.depthBiasConstantFactor = 0.f;
  info.depthBiasClamp = 0.f;
  info.depthBiasSlopeFactor = 0.f;

  return info;
}

VkPipelineMultisampleStateCreateInfo PipelineMultisampleStateCreateInfo() {
  VkPipelineMultisampleStateCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  info.pNext = nullptr;

  info.sampleShadingEnable = VK_FALSE;

  // No multisampling.
  info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  info.minSampleShading = 1.f;
  info.pSampleMask = nullptr;
  info.alphaToCoverageEnable = VK_FALSE;
  info.alphaToOneEnable = VK_FALSE;

  return info;
}

VkPipelineColorBlendAttachmentState PipelineColorBlendAttachmentState() {
  VkPipelineColorBlendAttachmentState attachment = {};
  attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  attachment.blendEnable = VK_FALSE;

  return attachment;
}

VkPipelineDepthStencilStateCreateInfo PipelineDepthStencilStateCreateInfo(
    bool depth_test, bool depth_write, VkCompareOp compare_op) {
  VkPipelineDepthStencilStateCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  info.pNext = nullptr;

  info.depthTestEnable = depth_test ? VK_TRUE : VK_FALSE;
  info.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
  info.depthCompareOp = depth_test ? compare_op : VK_COMPARE_OP_ALWAYS;

  info.depthBoundsTestEnable = VK_FALSE;
  info.minDepthBounds = 0.f;
  info.maxDepthBounds = 1.f;
  info.stencilTestEnable = VK_FALSE;

  return info;
}

VkImageCreateInfo ImageCreateInfo(VkFormat format,
                                  VkImageUsageFlags usage_flags,
                                  VkExtent3D extent) {
  VkImageCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  info.pNext = nullptr;

  info.imageType = VK_IMAGE_TYPE_2D;

  info.format = format;
  info.extent = extent;

  info.mipLevels = 1;
  info.arrayLayers = 1;
  // No MSAA.
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  // How the data for the texture is arranged. Since we're not reading it on the
  // CPU, this will be fine.
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = usage_flags;

  return info;
}

VkImageViewCreateInfo ImageViewCreateInfo(VkFormat format, VkImage image,
                                          VkImageAspectFlags aspect_flags) {
  VkImageViewCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  info.pNext = nullptr;

  info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  info.image = image;
  info.format = format;

  info.subresourceRange.baseMipLevel = 0;
  info.subresourceRange.levelCount = 1;
  info.subresourceRange.baseArrayLayer = 0;
  info.subresourceRange.layerCount = 1;
  info.subresourceRange.aspectMask = aspect_flags;

  return info;
}

VkCommandBufferAllocateInfo CommandBufferAllocateInfo(
    VkCommandPool command_pool, uint32_t count) {
  VkCommandBufferAllocateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  info.pNext = nullptr;
  info.commandPool = command_pool;
  info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  info.commandBufferCount = count;

  return info;
}

VkFenceCreateInfo FenceCreateInfo(VkFenceCreateFlags flags) {
  VkFenceCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  info.pNext = nullptr;

  info.flags = flags;

  return info;
}

VkSemaphoreCreateInfo SemaphoreCreateInfo() {
  VkSemaphoreCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  info.pNext = nullptr;

  info.flags = 0;

  return info;
}

}  // namespace init
}  // namespace vk