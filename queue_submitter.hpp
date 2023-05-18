#pragma once

#include <vulkan/vulkan.h>

#include <functional>

namespace vk {

struct UploadContext {
  VkFence fence;
  VkCommandPool command_pool;
  VkCommandBuffer command_buffer;
};

class QueueSubmitter {
 public:
  explicit QueueSubmitter(VkDevice device, VkQueue queue,
                          UploadContext upload_context)
      : device_(device), queue_(queue), upload_context_(upload_context) {}

  void SubmitImmediate(std::function<void(VkCommandBuffer cmd)>&& function);

 private:
  VkDevice device_;
  VkQueue queue_;
  UploadContext upload_context_;
};

}  // namespace vk
