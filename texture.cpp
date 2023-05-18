#include "texture.hpp"

#include <string.h>

#include "buffer.hpp"
#include "vk_init.hpp"

namespace vk {

Texture Texture::CreateFromLocalBuffer(VmaAllocator allocator, VkDevice device,
                                       QueueSubmitter& queue_submitter,
                                       unsigned char* buffer,
                                       size_t buffer_size,
                                       TextureProperties properties,
                                       VkFormat format) {
  return Texture(allocator, device, queue_submitter, buffer, buffer_size,
                 properties, format);
}

Texture::Texture(VmaAllocator allocator, VkDevice device,
                 QueueSubmitter& queue_submitter, unsigned char* buffer,
                 size_t buffer_size, TextureProperties properties,
                 VkFormat format)
    : device_{device}, allocator_(allocator) {
  // Staging buffer used to copy texture to GPU-only memory.
  AllocatedBuffer staging_buffer =
      CreateBuffer(allocator, buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VMA_MEMORY_USAGE_CPU_ONLY);

  void* data;
  vmaMapMemory(allocator, staging_buffer.allocation, &data);
  memcpy(data, buffer, buffer_size);
  vmaUnmapMemory(allocator, staging_buffer.allocation);

  VkExtent3D image_extent;
  image_extent.width = static_cast<uint32_t>(properties.width);
  image_extent.height = static_cast<uint32_t>(properties.height);
  image_extent.depth = 1;

  VkImageCreateInfo image_info = init::ImageCreateInfo(
      format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      image_extent);

  VmaAllocationCreateInfo allocation_info = {};
  allocation_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

  vmaCreateImage(allocator, &image_info, &allocation_info, &image_.image,
                 &image_.allocation, nullptr);

  queue_submitter.SubmitImmediate([&](VkCommandBuffer cmd) {
    VkImageSubresourceRange range;
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    // Image barrier to act as a pipeline barrier (i.e. control how GPU overlaps
    // commands) as well as allowing us to do conversion to correct
    // formats/layouts.
    VkImageMemoryBarrier transfer_barrier = {};
    transfer_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    transfer_barrier.pNext = nullptr;

    transfer_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    transfer_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    transfer_barrier.image = image_.image;
    transfer_barrier.subresourceRange = range;

    transfer_barrier.srcAccessMask = 0;
    transfer_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &transfer_barrier);

    VkBufferImageCopy copy_region = {};
    copy_region.bufferOffset = 0;
    copy_region.bufferRowLength = 0;
    copy_region.bufferImageHeight = 0;

    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.mipLevel = 0;
    copy_region.imageSubresource.baseArrayLayer = 0;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageExtent = image_extent;

    // Copy the buffer into the image.
    vkCmdCopyBufferToImage(cmd, staging_buffer.buffer, image_.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &copy_region);

    VkImageMemoryBarrier readable_barrier = transfer_barrier;
    readable_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    readable_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    readable_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    readable_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    // Barrier the image into the shader readable layout.
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &readable_barrier);
  });

  vmaDestroyBuffer(allocator, staging_buffer.buffer, staging_buffer.allocation);

  VkImageViewCreateInfo image_view_create_info = init::ImageViewCreateInfo(
      format, image_.image, VK_IMAGE_ASPECT_COLOR_BIT);

  vkCreateImageView(device, &image_view_create_info, nullptr, &image_view_);
}

Texture::~Texture() {
  if (allocator_) {
    vkDestroyImageView(device_, image_view_, nullptr);
    vmaDestroyImage(allocator_, image_.image, image_.allocation);
  }
}

}  // namespace vk
