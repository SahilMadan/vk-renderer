#include "buffer.hpp"

#include <iostream>

namespace vk {

AllocatedBuffer CreateBuffer(VmaAllocator allocator, size_t allocation_size,
                             VkBufferUsageFlags usage,
                             VmaMemoryUsage memory_usage) {
  // Allocate the vertex buffer.
  VkBufferCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.pNext = nullptr;

  info.size = allocation_size;
  info.usage = usage;

  VmaAllocationCreateInfo vma_allocation_info = {};
  vma_allocation_info.usage = memory_usage;

  AllocatedBuffer buffer;

  if (vmaCreateBuffer(allocator, &info, &vma_allocation_info, &buffer.buffer,
                      &buffer.allocation, nullptr) != VK_SUCCESS) {
    std::cerr << "Error creating buffer of size: " << allocation_size
              << std::endl;
    abort();
  }

  return buffer;
}

}  // namespace vk