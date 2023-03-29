#pragma once

#include <string>
#include <vector>
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "task_stack.hpp"

namespace vk {

class Renderer {
 public:

  struct InitParams {
    int width;
    int height;
    std::string application_name;

    HWND window_handle;

    std::vector<const char*> extensions;
  };

  // Lifetime events.
  bool Init(InitParams params);
  void Draw();
  void Shutdown();

  // Accessors.
  bool initialized() { return initialized_; }
  int framenumber() { return framenumber_; }

 private:
  bool initialized_ = false;
  int framenumber_ = 0;

  VkExtent2D render_extent_;

  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;

  VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;

  VkQueue graphics_queue_ = VK_NULL_HANDLE;
  uint32_t graphics_queue_family_ = 0;

  util::TaskStack deletion_stack_;
};

}  // namespace vk
