#pragma once

#include <optional>
#include <string>
#include <vector>
#define VK_USE_PLATFORM_WIN32_KHR
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include "task_stack.hpp"
#include "vk_mesh.hpp"

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
  struct PipelineBuilder {
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    VkPipelineVertexInputStateCreateInfo vertex_input_info;
    VkPipelineInputAssemblyStateCreateInfo input_assembly;
    VkPipelineDepthStencilStateCreateInfo depth_stencil;
    VkViewport viewport;
    VkRect2D scissor;
    VkPipelineRasterizationStateCreateInfo rasterizer;
    VkPipelineColorBlendAttachmentState color_blend_attachment;
    VkPipelineMultisampleStateCreateInfo multisampling;
    VkPipelineLayout layout;

    std::optional<VkPipeline> Build(VkDevice device, VkRenderPass renderpass);
  };

  bool InitPipeline();
  bool LoadMeshes();
  bool UploadMesh(Mesh& mesh);

  bool initialized_ = false;
  int framenumber_ = 0;

  VkExtent2D swapchain_extent_;

  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;

  VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;

  VkQueue graphics_queue_ = VK_NULL_HANDLE;
  uint32_t graphics_queue_family_ = 0;

  VkSwapchainKHR swapchain_;
  VkFormat swapchain_image_format_;
  std::vector<VkImage> swapchain_images_;
  std::vector<VkImageView> swapchain_image_views_;

  VkCommandPool command_pool_;
  VkCommandBuffer command_buffer_;

  VkRenderPass renderpass_;
  std::vector<VkFramebuffer> framebuffers_;

  // Semaphore used for GPU to GPU sync.
  VkSemaphore present_semaphore_;
  VkSemaphore render_semaphore_;
  // Used for GPU to CPU communication.
  VkFence render_fence_;

  VkPipelineLayout mesh_pipeline_layout_;
  VkPipeline mesh_pipeline_;

  VkImageView depth_image_view_;
  AllocatedImage depth_image_;
  VkFormat depth_format_;

  VmaAllocator allocator_;

  util::TaskStack deletion_stack_;

  std::vector<Mesh> shiba_mesh_;
};

}  // namespace vk
