#pragma once

#include <vk_mem_alloc.h>

namespace vk {

struct AllocatedImage {
  VkImage image;
  VmaAllocation allocation;
};

}  // namespace vk
