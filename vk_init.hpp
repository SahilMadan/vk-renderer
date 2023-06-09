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

VkCommandBufferAllocateInfo CommandBufferAllocateInfo(
    VkCommandPool command_pool, uint32_t count);

VkFenceCreateInfo FenceCreateInfo(VkFenceCreateFlags flags = 0);

VkSemaphoreCreateInfo SemaphoreCreateInfo();

VkDescriptorSetLayoutBinding DescriptorSetLayoutBinding(
    VkDescriptorType type, VkShaderStageFlags stage_flags, uint32_t binding);

VkWriteDescriptorSet WriteDescriptorSet(VkDescriptorType type,
                                        VkDescriptorSet destination_set,
                                        VkDescriptorBufferInfo* buffer_info,
                                        uint32_t binding);

VkCommandPoolCreateInfo CommandPoolCreateInfo(
    uint32_t graphics_queue_family, VkCommandPoolCreateFlags flags = 0);

VkCommandBufferBeginInfo CommandBufferBeginInfo(
    VkCommandBufferUsageFlags flags = 0);

VkSubmitInfo SubmitInfo(VkCommandBuffer* command_buffer);

};  // namespace init

};  // namespace vk
