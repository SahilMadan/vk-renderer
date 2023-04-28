#pragma once

#include <vulkan/vulkan.h>

namespace vk {
namespace init {

VkPipelineShaderStageCreateInfo PipelineShaderStageCreateInfo(
    VkShaderStageFlagBits stage, VkShaderModule module);

VkPipelineLayoutCreateInfo PipelineLayoutCreateInfo();

VkPipelineVertexInputStateCreateInfo PipelineVertexInputStateCreateInfo();

VkPipelineInputAssemblyStateCreateInfo PipelineInputAssemblyStateCreateInfo(
    VkPrimitiveTopology topology);

VkPipelineRasterizationStateCreateInfo PipelineRasterizationStateCreateInfo(
    VkPolygonMode polygon_mode);

VkPipelineMultisampleStateCreateInfo PipelineMultisampleStateCreateInfo();

VkPipelineColorBlendAttachmentState PipelineColorBlendAttachmentState();

VkPipelineDepthStencilStateCreateInfo PipelineDepthStencilStateCreateInfo(
    bool depth_test, bool depth_write, VkCompareOp compare_op);

VkImageCreateInfo ImageCreateInfo(VkFormat format,
                                  VkImageUsageFlags usage_flags,
                                  VkExtent3D extent);

VkImageViewCreateInfo ImageViewCreateInfo(VkFormat format, VkImage image,
                                          VkImageAspectFlags aspect_flags);

};  // namespace init

};  // namespace vk
