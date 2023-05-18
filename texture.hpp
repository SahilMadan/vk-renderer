#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include "queue_submitter.hpp"
#include "task_stack.hpp"
#include "vk_types.hpp"

namespace vk {

struct TextureProperties {
  size_t width;
  size_t height;
};

class Texture {
 public:
  static Texture CreateFromLocalBuffer(VmaAllocator allocator, VkDevice device,
                                       QueueSubmitter& queue_submitter,
                                       unsigned char* buffer,
                                       size_t buffer_size,
                                       TextureProperties properties,
                                       VkFormat format);

  Texture(Texture&& other) noexcept
      : device_{other.device_},
        allocator_{other.allocator_},
        image_{other.image_},
        image_view_{other.image_view_} {
    other.device_ = VK_NULL_HANDLE;
    other.allocator_ = nullptr;
    other.image_.image = VK_NULL_HANDLE;
    other.image_.allocation = nullptr;
    other.image_view_ = VK_NULL_HANDLE;
  }

  ~Texture();

  Texture& operator=(Texture& other) noexcept {
    device_ = other.device_;
    allocator_ = other.allocator_;
    image_ = std::move(other.image_);
    image_view_ = std::move(other.image_view_);

    other.device_ = VK_NULL_HANDLE;
    other.allocator_ = nullptr;
    other.image_.image = VK_NULL_HANDLE;
    other.image_.allocation = nullptr;
    other.image_view_ = VK_NULL_HANDLE;
  }

 private:
  VkDevice device_;
  VmaAllocator allocator_;
  AllocatedImage image_;
  VkImageView image_view_;

  Texture(VmaAllocator allocator, VkDevice device,
          QueueSubmitter& queue_submitter, unsigned char* buffer,
          size_t buffer_size, TextureProperties properties, VkFormat format);
};

}  // namespace vk
