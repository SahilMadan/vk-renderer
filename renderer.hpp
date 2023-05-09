#pragma once

#include <optional>
#include <string>
#include <unordered_map>
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

  struct Material {
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
  };

  struct RenderObject {
    Mesh* mesh;
    Material* material;
    glm::mat4 transform;
  };

  struct GpuSceneData {
    glm::vec4 fog_color;
    glm::vec4 fog_distance;
    glm::vec4 ambient_color;
    glm::vec4 sunlight_direction;
    glm::vec4 sunlight_color;
  };

  struct FrameData {
    // GPU <--> GPU sync.
    VkSemaphore present_semaphore;
    VkSemaphore render_semaphore;
    // GPU --> CPU sync.
    VkFence render_fence;

    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;

    // Buffer holding a single GPUCameraData to use when rendering.
    AllocatedBuffer camera_buffer;
    VkDescriptorSet global_descriptor;

    // Storage buffer for objects.
    AllocatedBuffer object_buffer;
    VkDescriptorSet object_descriptor;
  };

  constexpr static unsigned unsigned int kFrameOverlap = 2;

  bool InitPipeline();

  void InitScene();

  void InitDescriptors();

  bool LoadMeshes();
  bool UploadMesh(Mesh& mesh);

  size_t GetAlignedBufferSize(size_t original_size);

  AllocatedBuffer CreateBuffer(size_t allocation_size, VkBufferUsageFlags usage,
                               VmaMemoryUsage memory_usage);

  Material* CreateMaterial(VkPipeline pipeline, VkPipelineLayout layout,
                           const std::string& name);

  Material* GetMaterial(const std::string& name);
  Mesh* GetMesh(const std::string& name);

  FrameData& GetFrame();

  void DrawObjects(VkCommandBuffer cmd, RenderObject* first, int count);

  std::vector<RenderObject> renderables_;
  std::unordered_map<std::string, Material> materials_;
  std::unordered_map<std::string, Mesh> meshes_;

  bool initialized_ = false;
  int framenumber_ = 0;

  VkExtent2D swapchain_extent_;

  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties gpu_properties_;
  VkDevice device_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;

  VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;

  VkQueue graphics_queue_ = VK_NULL_HANDLE;
  uint32_t graphics_queue_family_ = 0;

  VkSwapchainKHR swapchain_;
  VkFormat swapchain_image_format_;
  std::vector<VkImage> swapchain_images_;
  std::vector<VkImageView> swapchain_image_views_;

  FrameData frames_[kFrameOverlap];

  VkRenderPass renderpass_;
  std::vector<VkFramebuffer> framebuffers_;

  VkPipelineLayout mesh_pipeline_layout_;
  VkPipeline mesh_pipeline_;

  VkImageView depth_image_view_;
  AllocatedImage depth_image_;
  VkFormat depth_format_;

  VmaAllocator allocator_;

  VkDescriptorSetLayout global_set_layout_;
  VkDescriptorSetLayout object_set_layout_;
  VkDescriptorPool descriptor_pool_;

  util::TaskStack deletion_stack_;

  Mesh triangle_mesh_;
  std::vector<Mesh> shiba_mesh_;

  GpuSceneData scene_parameters_;
  AllocatedBuffer scene_parameters_buffer_;
};

}  // namespace vk
