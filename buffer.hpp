#pragma once

#include <vk_mem_alloc.h>

namespace vk {

struct AllocatedBuffer {
  VkBuffer buffer;
  VmaAllocation allocation;
};

AllocatedBuffer CreateBuffer(VmaAllocator allocator, size_t allocation_size,
                             VkBufferUsageFlags usage,
                             VmaMemoryUsage memory_usage);

}  // namespace vk
