
#include "queue_submitter.hpp"

#include <iostream>

#include "vk_init.hpp"

namespace vk {

void QueueSubmitter::SubmitImmediate(
    std::function<void(VkCommandBuffer cmd)>&& function) {
  VkCommandBufferBeginInfo begin_info =
      init::CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

  if (vkBeginCommandBuffer(upload_context_.command_buffer, &begin_info) !=
      VK_SUCCESS) {
    std::cerr << "Error beginning submit immediate command.\n";
    abort();
  }

  function(upload_context_.command_buffer);

  if (vkEndCommandBuffer(upload_context_.command_buffer) != VK_SUCCESS) {
    std::cerr << "Error ending submit immediate command.\n";
    abort();
  }

  VkSubmitInfo submit = init::SubmitInfo(&upload_context_.command_buffer);

  if (vkQueueSubmit(queue_, 1, &submit, upload_context_.fence) != VK_SUCCESS) {
    std::cerr << "Error performing submit immediate queue submit.\n";
    abort();
  }

  vkWaitForFences(device_, 1, &upload_context_.fence, true, 9'999'999'999);
  vkResetFences(device_, 1, &upload_context_.fence);

  vkResetCommandPool(device_, upload_context_.command_pool, 0);
}

}  // namespace vk