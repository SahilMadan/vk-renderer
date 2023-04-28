#pragma once

#include <vk_mem_alloc.h>

namespace vk {

struct AllocatedBuffer {
  VkBuffer buffer;
  VmaAllocation allocation;
};

struct AllocatedImage {
  VkImage image;
  VmaAllocation allocation;
};

}  // namespace vk
