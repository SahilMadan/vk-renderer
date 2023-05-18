#include "renderer.hpp"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <glm/gtx/transform.hpp>
#include <iostream>
#include <optional>
#include <unordered_set>

#include "shader.hpp"
#include "vk_init.hpp"

namespace {

struct GpuCameraData {
  glm::mat4 view;
  glm::mat4 projection;
  glm::mat4 view_projection;
};

struct GpuObjectData {
  glm::mat4 model;
};

constexpr uint64_t kTimeoutNanoSecs = 1000000000;

#ifdef _DEBUG
constexpr bool kEnableValidationLayers = true;
#else
constexpr bool kEnableValidationLayers = false;
#endif

const std::vector<const char*> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation"};

const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
};

bool VerifyValidationLayersSupported(const std::vector<const char*>& layers) {
  uint32_t layer_count;
  vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
  std::vector<VkLayerProperties> available_layers(layer_count);
  vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

  for (const char* layer_name : layers) {
    bool layer_found = false;
    for (const auto& properties : available_layers) {
      if (strcmp(layer_name, properties.layerName) == 0) {
        layer_found = true;
        break;
      }
    }

    if (!layer_found) return false;
  }
  return true;
}

bool VerifyDeviceExtensionsSupported(VkPhysicalDevice device) {
  uint32_t count;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> available(count);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &count,
                                       available.data());

  int supported = 0;
  for (const auto& extension : available) {
    for (const char* required : kDeviceExtensions) {
      if (strcmp(extension.extensionName, required)) {
        supported++;
        break;
      }
    }
    if (supported == kDeviceExtensions.size()) return true;
  }

  return false;
}

struct SwapchainDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> present_modes;
};

struct SelectedDeviceDetails {
  VkPhysicalDevice device;
  VkPhysicalDeviceProperties properties;
  uint32_t graphics_queue_family;
  SwapchainDetails swapchain_details;
};

std::optional<uint32_t> GetQueueFamilyIndices(VkPhysicalDevice device,
                                              VkSurfaceKHR& surface) {
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);

  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
  std::optional<uint32_t> result = std::nullopt;
  for (int i = 0; i < count; i++) {
    VkBool32 present_supported = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface,
                                         &present_supported);
    if (present_supported && families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      result = i;
    }
  }
  return result;
}

std::optional<SwapchainDetails> GetSwapchainDetails(VkPhysicalDevice device,
                                                    VkSurfaceKHR surface) {
  // We need to query three properties.
  // 1. Basic surface capabilities (min/max number of images in swapchain,
  // width/height of images).
  // 2. Surface formats (pixel format, color space).
  // 3. Available presentation modes.

  SwapchainDetails details;

  uint32_t format_count;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);
  if (format_count == 0) {
    return std::nullopt;
  }
  details.formats.resize(format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count,
                                       details.formats.data());

  uint32_t present_mode_count;
  vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface,
                                            &present_mode_count, nullptr);
  if (present_mode_count == 0) {
    return std::nullopt;
  }
  details.present_modes.resize(present_mode_count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(
      device, surface, &present_mode_count, details.present_modes.data());

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface,
                                            &details.capabilities);

  return details;
}

VkSurfaceFormatKHR SelectSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& available_formats) {
  // Prefer non-linear SRGB if available.
  for (const auto& format : available_formats) {
    if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
        format.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
      return format;
    }
  }
  // Choose non-optimal whatever is available.
  return available_formats[0];
}

VkPresentModeKHR SelectSwapPresentMode(
    const std::vector<VkPresentModeKHR>& available_modes) {
  for (const auto mode : available_modes) {
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      return mode;
    }
  }
  // Guaranteed to be available.
  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D SelectSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                            uint32_t width, uint32_t height) {
  if (capabilities.currentExtent.width != INT_MAX) {
    return capabilities.currentExtent;
  }
  VkExtent2D extent = {width, height};
  extent.width = std::clamp(extent.width, capabilities.minImageExtent.width,
                            capabilities.maxImageExtent.width);
  extent.height = std::clamp(extent.height, capabilities.minImageExtent.height,
                             capabilities.maxImageExtent.height);
  return extent;
}

std::optional<SelectedDeviceDetails> SelectDevice(
    std::vector<VkPhysicalDevice>& devices, VkSurfaceKHR& surface) {
  std::vector<int> scores(devices.size());
  int max_score = 0;
  int max_index = -1;
  uint32_t max_queue_family = 0;
  SwapchainDetails max_swapchain_details;
  for (int i = 0; i < devices.size(); i++) {
    VkPhysicalDevice device = devices[i];
    std::optional<uint32_t> queue_family =
        GetQueueFamilyIndices(device, surface);
    // Get the graphics queue. If there isn't one, this device isn't supported.
    if (!queue_family.has_value()) {
      continue;
    }

    if (!VerifyDeviceExtensionsSupported(device)) {
      continue;
    }

    std::optional<SwapchainDetails> swapchain_details =
        GetSwapchainDetails(device, surface);
    if (!swapchain_details.has_value()) {
      continue;
    }

    //  Get device properties.
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);

    // We want to support the SPIR-V DrawParameters capability.
    VkPhysicalDeviceShaderDrawParametersFeatures ext_feature = {};
    ext_feature.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
    ext_feature.pNext = nullptr;

    VkPhysicalDeviceFeatures2 features;
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &ext_feature;
    vkGetPhysicalDeviceFeatures2(device, &features);
    if (ext_feature.shaderDrawParameters == VK_FALSE) continue;
    if (features.features.geometryShader == VK_FALSE) continue;
    if (properties.apiVersion < VK_API_VERSION_1_1) continue;
    // Rate suitability.
    int score = 0;
    // Discrete GPUs have performance advantages.
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      score += 1000;
    }
    // Maximum possible size of textures.
    score += properties.limits.maxImageDimension2D;

    if (score > max_score) {
      max_score = score;
      max_index = i;
      max_queue_family = queue_family.value();
      max_swapchain_details = std::move(swapchain_details.value());
    }
  }
  if (max_index < 0) {
    // Unable to find a suitable device.
    return std::nullopt;
  }

  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(devices[max_index], &properties);

  return std::make_optional<SelectedDeviceDetails>(
      {devices[max_index], properties, max_queue_family,
       std::move(max_swapchain_details)});
}

}  // namespace

namespace vk {

bool Renderer::Init(InitParams params) {
  // Initialize Vulkan application.
  VkApplicationInfo app_info = {};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pNext = nullptr;
  app_info.pApplicationName = params.application_name.c_str();
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = "vk-renderer";
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.apiVersion = VK_API_VERSION_1_1;

  // Initialize Vulkan instance.
  VkInstanceCreateInfo instance_info = {};
  instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_info.pNext = nullptr;
  instance_info.pApplicationInfo = &app_info;
  instance_info.enabledExtensionCount = params.extensions.size();
  instance_info.ppEnabledExtensionNames = params.extensions.data();
  if (kEnableValidationLayers) {
    if (!VerifyValidationLayersSupported(kValidationLayers)) {
      return false;
    }
    instance_info.enabledLayerCount =
        static_cast<uint32_t>(kValidationLayers.size());
    instance_info.ppEnabledLayerNames = kValidationLayers.data();
  } else {
    instance_info.enabledLayerCount = 0;
  }

  if (vkCreateInstance(&instance_info, nullptr, &instance_) != VK_SUCCESS) {
    return false;
  }
  deletion_stack_.Push([=]() { vkDestroyInstance(instance_, nullptr); });

  // Create the surface.
  VkWin32SurfaceCreateInfoKHR surface_info = {};
  surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  surface_info.pNext = nullptr;
  surface_info.hwnd = params.window_handle;
  surface_info.hinstance = GetModuleHandle(nullptr);
  if (vkCreateWin32SurfaceKHR(instance_, &surface_info, nullptr, &surface_) !=
      VK_SUCCESS) {
    return false;
  }
  deletion_stack_.Push(
      [&]() { vkDestroySurfaceKHR(instance_, surface_, nullptr); });

  // Initialize the physical gpu.
  uint32_t device_count = 0;
  vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
  if (device_count == 0) {
    return false;
  }
  std::vector<VkPhysicalDevice> devices(device_count);
  vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());
  std::optional<SelectedDeviceDetails> maybe_selected_device =
      SelectDevice(devices, surface_);
  if (!maybe_selected_device.has_value()) {
    return false;
  }
  SelectedDeviceDetails& selected_device = maybe_selected_device.value();
  gpu_ = selected_device.device;
  gpu_properties_ = selected_device.properties;

  // Initialize the device queue.
  VkDeviceQueueCreateInfo queue_info = {};
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_info.pNext = nullptr;
  queue_info.queueFamilyIndex = selected_device.graphics_queue_family;
  queue_info.queueCount = 1;
  float queue_priority = 1.0f;  // Highest priority since we only have 1.
  queue_info.pQueuePriorities = &queue_priority;
  graphics_queue_family_ = selected_device.graphics_queue_family;

  // Initialize the logical device.
  VkPhysicalDeviceFeatures device_features = {};
  device_features.geometryShader = VK_TRUE;

  VkDeviceCreateInfo device_info = {};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.pNext = nullptr;

  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  device_info.pEnabledFeatures = &device_features;
  device_info.enabledExtensionCount =
      static_cast<uint32_t>(kDeviceExtensions.size());
  device_info.ppEnabledExtensionNames = kDeviceExtensions.data();

  if (kEnableValidationLayers) {
    device_info.enabledLayerCount =
        static_cast<uint32_t>(kValidationLayers.size());
    device_info.ppEnabledLayerNames = kValidationLayers.data();
  } else {
    device_info.enabledLayerCount = 0;
  }

  if (vkCreateDevice(gpu_, &device_info, nullptr, &device_) != VK_SUCCESS) {
    return false;
  }
  deletion_stack_.Push([&]() { vkDestroyDevice(device_, nullptr); });

  // Initialize memory allocator.
  VmaAllocatorCreateInfo allocator_info = {};
  allocator_info.physicalDevice = gpu_;
  allocator_info.device = device_;
  allocator_info.instance = instance_;
  vmaCreateAllocator(&allocator_info, &allocator_);
  deletion_stack_.Push([&]() { vmaDestroyAllocator(allocator_); });

  // Initialize the graphics queue.
  vkGetDeviceQueue(device_, graphics_queue_family_, 0, &graphics_queue_);

  // Initialize the swapchain.
  VkSurfaceFormatKHR surface_format =
      SelectSwapSurfaceFormat(selected_device.swapchain_details.formats);
  VkPresentModeKHR present_mode =
      SelectSwapPresentMode(selected_device.swapchain_details.present_modes);
  swapchain_extent_ =
      SelectSwapExtent(selected_device.swapchain_details.capabilities,
                       params.width, params.height);
  swapchain_image_format_ = surface_format.format;

  uint32_t image_count = std::clamp(
      selected_device.swapchain_details.capabilities.minImageCount + 1,
      selected_device.swapchain_details.capabilities.minImageCount,
      selected_device.swapchain_details.capabilities.maxImageCount);

  VkSwapchainCreateInfoKHR swapchain_info = {};
  swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchain_info.pNext = nullptr;

  swapchain_info.surface = surface_;
  swapchain_info.minImageCount = image_count;
  swapchain_info.imageFormat = surface_format.format;
  swapchain_info.imageColorSpace = surface_format.colorSpace;
  swapchain_info.imageExtent = swapchain_extent_;
  swapchain_info.imageArrayLayers = 1;
  swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  // Currently, we're using the same queue for graphics and presentation. This
  // would change if we weren't.
  swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchain_info.queueFamilyIndexCount = 0;
  swapchain_info.pQueueFamilyIndices = nullptr;

  // I.e. no rotation, etc.
  swapchain_info.preTransform =
      selected_device.swapchain_details.capabilities.currentTransform;
  // Alpha channel used for blending with other windows.
  swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapchain_info.presentMode = present_mode;
  swapchain_info.clipped = VK_TRUE;
  // Specified when the window size changes.
  swapchain_info.oldSwapchain = VK_NULL_HANDLE;

  if (vkCreateSwapchainKHR(device_, &swapchain_info, nullptr, &swapchain_) !=
      VK_SUCCESS) {
    return false;
  }
  deletion_stack_.Push(
      [=]() { vkDestroySwapchainKHR(device_, swapchain_, nullptr); });

  uint32_t swapchain_image_count;
  vkGetSwapchainImagesKHR(device_, swapchain_, &swapchain_image_count, nullptr);
  swapchain_images_.resize(swapchain_image_count);
  vkGetSwapchainImagesKHR(device_, swapchain_, &swapchain_image_count,
                          swapchain_images_.data());

  // Initialize the depth image.
  VkExtent3D depth_image_extent = {swapchain_extent_.width,
                                   swapchain_extent_.height, 1};
  depth_format_ = VK_FORMAT_D32_SFLOAT;

  VkImageCreateInfo depth_image_info = init::ImageCreateInfo(
      depth_format_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      depth_image_extent);

  VmaAllocationCreateInfo depth_allocation_info = {};
  depth_allocation_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  depth_allocation_info.requiredFlags =
      VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  vmaCreateImage(allocator_, &depth_image_info, &depth_allocation_info,
                 &depth_image_.image, &depth_image_.allocation, nullptr);
  deletion_stack_.Push([=]() {
    vmaDestroyImage(allocator_, depth_image_.image, depth_image_.allocation);
  });

  VkImageViewCreateInfo depth_image_view_info = init::ImageViewCreateInfo(
      depth_format_, depth_image_.image, VK_IMAGE_ASPECT_DEPTH_BIT);

  if (vkCreateImageView(device_, &depth_image_view_info, nullptr,
                        &depth_image_view_) != VK_SUCCESS) {
    return false;
  }
  deletion_stack_.Push(
      [=]() { vkDestroyImageView(device_, depth_image_view_, nullptr); });

  // Initialize the Image Views.
  VkImageViewCreateInfo image_view_info = {};
  image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  image_view_info.pNext = nullptr;
  image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  image_view_info.format = swapchain_image_format_;
  image_view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  image_view_info.subresourceRange.baseMipLevel = 0;
  image_view_info.subresourceRange.levelCount = 1;
  image_view_info.subresourceRange.baseArrayLayer = 0;
  image_view_info.subresourceRange.layerCount = 1;

  swapchain_image_views_.resize(swapchain_image_count);
  for (int i = 0; i < swapchain_image_views_.size(); i++) {
    image_view_info.image = swapchain_images_[i];
    if (vkCreateImageView(device_, &image_view_info, nullptr,
                          &swapchain_image_views_[i]) != VK_SUCCESS) {
      return false;
    }

    deletion_stack_.Push([=]() {
      vkDestroyImageView(device_, swapchain_image_views_[i], nullptr);
    });
  }

  // Initialize the commands.
  VkCommandPoolCreateInfo command_pool_info = init::CommandPoolCreateInfo(
      graphics_queue_family_, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

  for (int i = 0; i < kFrameOverlap; i++) {
    if (vkCreateCommandPool(device_, &command_pool_info, nullptr,
                            &frames_[i].command_pool) != VK_SUCCESS) {
      return false;
    }

    deletion_stack_.Push([=]() {
      vkDestroyCommandPool(device_, frames_[i].command_pool, nullptr);
    });

    VkCommandBufferAllocateInfo allocate_info =
        init::CommandBufferAllocateInfo(frames_[i].command_pool, 1);

    if (vkAllocateCommandBuffers(device_, &allocate_info,
                                 &frames_[i].command_buffer) != VK_SUCCESS) {
      return false;
    }
  }

  UploadContext upload_context;

  VkCommandPoolCreateInfo upload_command_pool_info =
      init::CommandPoolCreateInfo(graphics_queue_family_);
  if (vkCreateCommandPool(device_, &upload_command_pool_info, nullptr,
                          &upload_context.command_pool) != VK_SUCCESS) {
    return false;
  }
  deletion_stack_.Push([=]() {
    vkDestroyCommandPool(device_, upload_context.command_pool, nullptr);
  });

  VkCommandBufferAllocateInfo upload_allocate_info =
      init::CommandBufferAllocateInfo(upload_context.command_pool, 1);

  if (vkAllocateCommandBuffers(device_, &upload_allocate_info,
                               &upload_context.command_buffer) != VK_SUCCESS) {
    return false;
  }

  // Initialize the default renderpass.
  VkAttachmentDescription color_attachment = {};
  color_attachment.format = swapchain_image_format_;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference color_attachment_ref = {};
  color_attachment_ref.attachment = 0;
  color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription depth_attachment = {};
  depth_attachment.flags = 0;
  depth_attachment.format = depth_format_;
  depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depth_attachment.finalLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depth_attachment_ref = {};
  depth_attachment_ref.attachment = 1;
  depth_attachment_ref.layout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_attachment_ref;
  subpass.pDepthStencilAttachment = &depth_attachment_ref;

  VkAttachmentDescription attachments[2] = {color_attachment, depth_attachment};

  VkSubpassDependency color_dependency = {};
  color_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  color_dependency.dstSubpass = 0;
  color_dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  color_dependency.srcAccessMask = 0;
  color_dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  color_dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkSubpassDependency depth_dependency = {};
  depth_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  depth_dependency.dstSubpass = 0;
  depth_dependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  depth_dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  depth_dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  VkSubpassDependency dependencies[2] = {color_dependency, depth_dependency};

  VkRenderPassCreateInfo renderpass_info = {};
  renderpass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderpass_info.pNext = nullptr;
  renderpass_info.attachmentCount = 2;
  renderpass_info.pAttachments = &attachments[0];
  renderpass_info.subpassCount = 1;
  renderpass_info.pSubpasses = &subpass;
  renderpass_info.dependencyCount = 2;
  renderpass_info.pDependencies = &dependencies[0];

  if (vkCreateRenderPass(device_, &renderpass_info, nullptr, &renderpass_)) {
    return false;
  }
  deletion_stack_.Push(
      [=]() { vkDestroyRenderPass(device_, renderpass_, nullptr); });

  // Initialize the framebuffers.
  VkFramebufferCreateInfo framebuffer_info = {};
  framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebuffer_info.pNext = nullptr;

  framebuffer_info.renderPass = renderpass_;
  framebuffer_info.attachmentCount = 1;
  framebuffer_info.width = swapchain_extent_.width;
  framebuffer_info.height = swapchain_extent_.height;
  framebuffer_info.layers = 1;

  framebuffers_.resize(swapchain_image_count);
  for (int i = 0; i < swapchain_image_count; i++) {
    VkImageView attachments[2];
    attachments[0] = swapchain_image_views_[i];
    attachments[1] = depth_image_view_;

    framebuffer_info.attachmentCount = 2;
    framebuffer_info.pAttachments = attachments;

    if (vkCreateFramebuffer(device_, &framebuffer_info, nullptr,
                            &framebuffers_[i])) {
      return false;
    }
    deletion_stack_.Push(
        [=]() { vkDestroyFramebuffer(device_, framebuffers_[i], nullptr); });
  }

  // Create synchronization structures.
  for (int i = 0; i < kFrameOverlap; i++) {
    VkFenceCreateInfo fence_info =
        init::FenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);

    if (vkCreateFence(device_, &fence_info, nullptr,
                      &frames_[i].render_fence) != VK_SUCCESS) {
      return false;
    }

    deletion_stack_.Push(
        [=]() { vkDestroyFence(device_, frames_[i].render_fence, nullptr); });

    VkSemaphoreCreateInfo semaphore_info = init::SemaphoreCreateInfo();

    if (vkCreateSemaphore(device_, &semaphore_info, nullptr,
                          &frames_[i].render_semaphore)) {
      return false;
    }

    deletion_stack_.Push([=]() {
      vkDestroySemaphore(device_, frames_[i].render_semaphore, nullptr);
    });

    if (vkCreateSemaphore(device_, &semaphore_info, nullptr,
                          &frames_[i].present_semaphore)) {
      return false;
    }

    deletion_stack_.Push([=]() {
      vkDestroySemaphore(device_, frames_[i].present_semaphore, nullptr);
    });
  }

  // We do not need to wait for this fence so we won't set
  // VK_FENCE_CREATE_SIGNALED_BIT.
  VkFenceCreateInfo upload_fence_create_info = init::FenceCreateInfo();
  if (vkCreateFence(device_, &upload_fence_create_info, nullptr,
                    &upload_context.fence) != VK_SUCCESS) {
    return false;
  }
  deletion_stack_.Push(
      [=]() { vkDestroyFence(device_, upload_context.fence, nullptr); });

  queue_submitter_ = std::make_unique<QueueSubmitter>(device_, graphics_queue_,
                                                      upload_context);
  InitDescriptors();

  if (!InitPipeline()) {
    return false;
  }

  if (!LoadMeshes()) {
    return false;
  }

  InitScene();

  // Everything is initialized.
  initialized_ = true;
}

void Renderer::Shutdown() {
  for (int i = 0; i < kFrameOverlap; i++) {
    if (vkGetFenceStatus(device_, frames_[i].render_fence) &&
        vkWaitForFences(device_, 1, &frames_[i].render_fence, true,
                        kTimeoutNanoSecs) != VK_SUCCESS) {
      return;
    }
  }
  deletion_stack_.Flush();
}

void Renderer::Draw() {
  // TODO: Handle errors gracefully.

  FrameData& frame = GetFrame();

  // Wait until the GPU has finished rendering the last frame.
  if (vkWaitForFences(device_, 1, &frame.render_fence, true,
                      kTimeoutNanoSecs) != VK_SUCCESS) {
    return;
  }
  if (vkResetFences(device_, 1, &frame.render_fence) != VK_SUCCESS) {
    return;
  }

  // Request an image from the swapchain.
  uint32_t swapchain_image_index;
  if (vkAcquireNextImageKHR(device_, swapchain_, kTimeoutNanoSecs,
                            frame.present_semaphore, nullptr,
                            &swapchain_image_index) != VK_SUCCESS) {
    return;
  }

  // Now we can safely reset the command buffer.
  if (vkResetCommandBuffer(frame.command_buffer, 0) != VK_SUCCESS) {
    return;
  }

  VkCommandBufferBeginInfo begin_info = {};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.pNext = nullptr;
  begin_info.pInheritanceInfo = nullptr;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  if (vkBeginCommandBuffer(frame.command_buffer, &begin_info) != VK_SUCCESS) {
    return;
  }

  VkClearValue color_value;
  color_value.color = {{0.1f, 0.2f, 0.3f, 1.0f}};

  VkClearValue depth_value;
  depth_value.depthStencil.depth = 1.f;

  VkRenderPassBeginInfo renderpass_info = {};
  renderpass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderpass_info.pNext = nullptr;

  renderpass_info.renderPass = renderpass_;
  renderpass_info.renderArea.offset.x = 0;
  renderpass_info.renderArea.offset.y = 0;
  renderpass_info.renderArea.extent = swapchain_extent_;
  renderpass_info.framebuffer = framebuffers_[swapchain_image_index];

  VkClearValue clear_values[2] = {color_value, depth_value};

  renderpass_info.clearValueCount = 2;
  renderpass_info.pClearValues = &clear_values[0];

  vkCmdBeginRenderPass(frame.command_buffer, &renderpass_info,
                       VK_SUBPASS_CONTENTS_INLINE);

  DrawObjects(frame.command_buffer, renderables_.data(), renderables_.size());

  vkCmdEndRenderPass(frame.command_buffer);
  if (vkEndCommandBuffer(frame.command_buffer) != VK_SUCCESS) {
    return;
  }

  // Submit.
  VkSubmitInfo submit = {};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.pNext = nullptr;

  VkPipelineStageFlags wait_stage =
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

  submit.pWaitDstStageMask = &wait_stage;

  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &frame.present_semaphore;

  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &frame.render_semaphore;

  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &frame.command_buffer;

  if (vkQueueSubmit(graphics_queue_, 1, &submit, frame.render_fence) !=
      VK_SUCCESS) {
    return;
  }

  VkPresentInfoKHR present_info = {};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.pNext = nullptr;

  present_info.swapchainCount = 1;
  present_info.pSwapchains = &swapchain_;

  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = &frame.render_semaphore;

  present_info.pImageIndices = &swapchain_image_index;

  if (vkQueuePresentKHR(graphics_queue_, &present_info) != VK_SUCCESS) {
    return;
  }

  framenumber_++;
}

std::optional<VkPipeline> Renderer::PipelineBuilder::Build(
    VkDevice device, VkRenderPass renderpass) {
  VkPipelineViewportStateCreateInfo viewport_state = {};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.pNext = nullptr;

  // We don't support multiple viewports or scissors.
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = &viewport;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = &scissor;

  VkPipelineColorBlendStateCreateInfo color_blending = {};
  color_blending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blending.pNext = nullptr;

  // We aren't using transparent objects so the blending is "no blend".
  color_blending.logicOpEnable = VK_FALSE;
  color_blending.logicOp = VK_LOGIC_OP_COPY;
  color_blending.attachmentCount = 1;
  color_blending.pAttachments = &color_blend_attachment;

  VkGraphicsPipelineCreateInfo pipeline_info = {};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.pNext = nullptr;

  pipeline_info.stageCount = shader_stages.size();
  pipeline_info.pStages = shader_stages.data();
  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pColorBlendState = &color_blending;
  pipeline_info.layout = layout;
  pipeline_info.renderPass = renderpass;
  pipeline_info.subpass = 0;
  pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
  pipeline_info.pDepthStencilState = &depth_stencil;

  VkPipeline pipeline;
  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                nullptr, &pipeline) != VK_SUCCESS) {
    return std::nullopt;
  }
  return pipeline;
}

bool Renderer::InitPipeline() {
  PipelineBuilder builder;

  builder.vertex_input_info = init::PipelineVertexInputStateCreateInfo();

  builder.input_assembly = init::PipelineInputAssemblyStateCreateInfo(
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

  builder.depth_stencil = init::PipelineDepthStencilStateCreateInfo(
      true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

  builder.viewport.x = 0.f;
  builder.viewport.y = 0.f;
  builder.viewport.width = static_cast<float>(swapchain_extent_.width);
  builder.viewport.height = static_cast<float>(swapchain_extent_.height);
  builder.viewport.minDepth = 0.f;
  builder.viewport.maxDepth = 1.f;

  builder.scissor.offset = {0, 0};
  builder.scissor.extent = swapchain_extent_;

  builder.rasterizer =
      init::PipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL);

  builder.multisampling = init::PipelineMultisampleStateCreateInfo();

  builder.color_blend_attachment = init::PipelineColorBlendAttachmentState();

  VkPipelineLayoutCreateInfo mesh_pipeline_layout_info =
      init::PipelineLayoutCreateInfo();

  VkPushConstantRange push_constant;
  push_constant.offset = 0;
  push_constant.size = sizeof(MeshPushConstants);
  push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  mesh_pipeline_layout_info.pushConstantRangeCount = 1;
  mesh_pipeline_layout_info.pPushConstantRanges = &push_constant;

  VkDescriptorSetLayout set_layouts[] = {global_set_layout_,
                                         object_set_layout_};

  mesh_pipeline_layout_info.setLayoutCount = 2;
  mesh_pipeline_layout_info.pSetLayouts = set_layouts;

  if (vkCreatePipelineLayout(device_, &mesh_pipeline_layout_info, nullptr,
                             &mesh_pipeline_layout_) != VK_SUCCESS) {
    return false;
  }
  deletion_stack_.Push([=]() {
    vkDestroyPipelineLayout(device_, mesh_pipeline_layout_, nullptr);
  });

  builder.layout = mesh_pipeline_layout_;

  VertexInputDescription vertex_description = Vertex::GetDescription();

  // Connect the pipeline builder vertex input info to the one from the vertex.
  builder.vertex_input_info.vertexAttributeDescriptionCount =
      vertex_description.attributes.size();
  builder.vertex_input_info.pVertexAttributeDescriptions =
      vertex_description.attributes.data();

  builder.vertex_input_info.vertexBindingDescriptionCount =
      vertex_description.bindings.size();
  builder.vertex_input_info.pVertexBindingDescriptions =
      vertex_description.bindings.data();

  VkShaderModule mesh_vert;
  if (!LoadShader(device_, "shaders/mesh_triangle.vert.spv", &mesh_vert)) {
    std::cerr << "Unable to load file: mesh_triangle.vert.spv" << std::endl;
    return false;
  }
  VkShaderModule mesh_frag;
  if (!LoadShader(device_, "shaders/default_lit.frag.spv", &mesh_frag)) {
    std::cerr << "Unable to load file: default_lit.frag.spv" << std::endl;
    return false;
  }

  builder.shader_stages.clear();
  builder.shader_stages.push_back(init::PipelineShaderStageCreateInfo(
      VK_SHADER_STAGE_VERTEX_BIT, mesh_vert));
  builder.shader_stages.push_back(init::PipelineShaderStageCreateInfo(
      VK_SHADER_STAGE_FRAGMENT_BIT, mesh_frag));

  std::optional<VkPipeline> maybe_pipeline =
      builder.Build(device_, renderpass_);
  if (!maybe_pipeline.has_value()) {
    return false;
  }

  mesh_pipeline_ = maybe_pipeline.value();
  deletion_stack_.Push(
      [=]() { vkDestroyPipeline(device_, mesh_pipeline_, nullptr); });

  CreateMaterial(mesh_pipeline_, mesh_pipeline_layout_, "default");

  vkDestroyShaderModule(device_, mesh_vert, nullptr);
  vkDestroyShaderModule(device_, mesh_frag, nullptr);

  return true;
}

bool Renderer::LoadMeshes() {
  triangle_mesh_.vertices.resize(3);

  // Vertex positions.
  triangle_mesh_.vertices[0].position = {1.f, 1.f, 0.f};
  triangle_mesh_.vertices[1].position = {-1.f, 1.f, 0.f};
  triangle_mesh_.vertices[2].position = {0.f, -1.f, 0.f};

  // Vertex colors.
  triangle_mesh_.vertices[0].color = {1.f, 1.f, 1.f};
  triangle_mesh_.vertices[1].color = {1.f, 1.f, 1.f};
  triangle_mesh_.vertices[2].color = {1.f, 1.f, 1.f};

  // Note: We don't care about vertex normals yet.

  if (!UploadMesh(triangle_mesh_)) {
    return false;
  }

  meshes_["triangle"] = triangle_mesh_;

  shiba_model_ = LoadFromFile("assets/models/shiba/scene.gltf", allocator_,
                              device_, *queue_submitter_.get());
  if (shiba_model_.meshes.empty()) {
    return false;
  }

  int count = 1;
  for (Mesh& m : shiba_model_.meshes) {
    if (!UploadMesh(m)) {
      return false;
    }
    std::string name = "shiba_" + std::to_string(count++);
    meshes_[name] = m;
  }

  deletion_stack_.Push([=]() { shiba_model_.textures.clear(); });

  return true;
}

bool Renderer::UploadMesh(Mesh& mesh) {
  util::TaskStack local_del;
  // Uploads mesh to GPU-only memory by first copying into CPU writeable buffer
  // and encoding a copy command in a VkCommandBuffer and submitting to a queue.
  // GPU native memory is much faster than CPU/GPU memory.

  // 1. Allocate a CPU side buffer to hold the mesh before uploading it to the
  // GPU.
  const size_t size = mesh.vertices.size() * sizeof(Vertex);

  // Allocate staging buffer.
  VkBufferCreateInfo staging_buffer_info = {};
  staging_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  staging_buffer_info.pNext = nullptr;

  staging_buffer_info.size = size;
  staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

  // Let VMA lib know that this data should be on CPU RAM.
  VmaAllocationCreateInfo vma_alloc_info = {};
  vma_alloc_info.usage = VMA_MEMORY_USAGE_CPU_ONLY;

  AllocatedBuffer staging_buffer;

  if (vmaCreateBuffer(allocator_, &staging_buffer_info, &vma_alloc_info,
                      &staging_buffer.buffer, &staging_buffer.allocation,
                      nullptr) != VK_SUCCESS) {
    return false;
  }
  local_del.Push([=]() {
    vmaDestroyBuffer(allocator_, staging_buffer.buffer,
                     staging_buffer.allocation);
  });

  // Copy the vertex data.
  void* data;
  vmaMapMemory(allocator_, staging_buffer.allocation, &data);
  memcpy(data, mesh.vertices.data(), size);
  vmaUnmapMemory(allocator_, staging_buffer.allocation);

  // 2. Allocate GPU side buffer.
  VkBufferCreateInfo vertex_buffer_info = {};
  vertex_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  vertex_buffer_info.pNext = nullptr;

  vertex_buffer_info.size = size;
  vertex_buffer_info.usage =
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

  // Let VMA lib know that this data should be GPU native.
  vma_alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

  if (vmaCreateBuffer(allocator_, &vertex_buffer_info, &vma_alloc_info,
                      &mesh.vertex_buffer.buffer,
                      &mesh.vertex_buffer.allocation, nullptr) != VK_SUCCESS) {
    return false;
  }

  deletion_stack_.Push([=]() {
    vmaDestroyBuffer(allocator_, mesh.vertex_buffer.buffer,
                     mesh.vertex_buffer.allocation);
  });

  queue_submitter_->SubmitImmediate([=](VkCommandBuffer cmd) {
    VkBufferCopy copy;
    copy.srcOffset = 0;
    copy.dstOffset = 0;
    copy.size = size;
    vkCmdCopyBuffer(cmd, staging_buffer.buffer, mesh.vertex_buffer.buffer, 1,
                    &copy);
  });

  if (mesh.indices.empty()) {
    return true;
  }

  // Repeat the above for the indices buffer.

  uint32_t indices_size = mesh.indices.size() * sizeof(uint32_t);

  vma_alloc_info.usage = VMA_MEMORY_USAGE_CPU_ONLY;

  AllocatedBuffer index_staging_buffer;

  if (vmaCreateBuffer(allocator_, &staging_buffer_info, &vma_alloc_info,
                      &index_staging_buffer.buffer,
                      &index_staging_buffer.allocation,
                      nullptr) != VK_SUCCESS) {
    return false;
  }
  local_del.Push([=]() {
    vmaDestroyBuffer(allocator_, index_staging_buffer.buffer,
                     index_staging_buffer.allocation);
  });

  // Copy the indices data.
  vmaMapMemory(allocator_, index_staging_buffer.allocation, &data);
  memcpy(data, mesh.indices.data(), indices_size);
  vmaUnmapMemory(allocator_, index_staging_buffer.allocation);

  VkBufferCreateInfo index_buffer_info = {};
  index_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  index_buffer_info.pNext = nullptr;

  index_buffer_info.size = indices_size;
  index_buffer_info.usage =
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

  vma_alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

  if (vmaCreateBuffer(allocator_, &index_buffer_info, &vma_alloc_info,
                      &mesh.index_buffer.buffer, &mesh.index_buffer.allocation,
                      nullptr) != VK_SUCCESS) {
    return false;
  }

  deletion_stack_.Push([=]() {
    vmaDestroyBuffer(allocator_, mesh.index_buffer.buffer,
                     mesh.index_buffer.allocation);
  });

  queue_submitter_->SubmitImmediate([=](VkCommandBuffer cmd) {
    VkBufferCopy copy;
    copy.srcOffset = 0;
    copy.dstOffset = 0;
    copy.size = indices_size;
    vkCmdCopyBuffer(cmd, index_staging_buffer.buffer, mesh.index_buffer.buffer,
                    1, &copy);
  });

  return true;
}

Renderer::Material* Renderer::CreateMaterial(VkPipeline pipeline,
                                             VkPipelineLayout layout,
                                             const std::string& name) {
  Material material;
  material.pipeline = pipeline;
  material.pipeline_layout = layout;
  materials_[name] = material;

  return &materials_[name];
}

Renderer::Material* Renderer::GetMaterial(const std::string& name) {
  auto it = materials_.find(name);
  if (it == materials_.end()) {
    return nullptr;
  }

  return &(*it).second;
}

Mesh* Renderer::GetMesh(const std::string& name) {
  auto it = meshes_.find(name);
  if (it == meshes_.end()) {
    return nullptr;
  }

  return &(*it).second;
}

void Renderer::DrawObjects(VkCommandBuffer cmd, RenderObject* first,
                           int count) {
  glm::vec3 camera_position = {0.f, -6.f, -10.f};

  glm::mat4 view = glm::translate(glm::mat4(1.f), camera_position);

  glm::mat4 projection =
      glm::perspective(glm::radians(70.f),
                       static_cast<float>(swapchain_extent_.width) /
                           static_cast<float>(swapchain_extent_.height),
                       0.1f, 200.0f);
  projection[1][1] *= -1;

  // Fill a GpuCameraData struct.
  GpuCameraData camera_data;
  camera_data.projection = projection;
  camera_data.view = view;
  camera_data.view_projection = projection * view;

  void* data;
  vmaMapMemory(allocator_, GetFrame().camera_buffer.allocation, &data);
  memcpy(data, &camera_data, sizeof(GpuCameraData));
  vmaUnmapMemory(allocator_, GetFrame().camera_buffer.allocation);

  // Scene data.
  float framed = framenumber_ / 120.f;
  scene_parameters_.ambient_color = {sin(framed), 1.f, cos(framed), 1.f};
  char* scene_data;
  vmaMapMemory(allocator_, scene_parameters_buffer_.allocation,
               reinterpret_cast<void**>(&scene_data));

  int frame_index = framenumber_ % kFrameOverlap;
  size_t buffer_offset =
      GetAlignedBufferSize(sizeof(GpuSceneData)) * frame_index;
  scene_data += buffer_offset;
  memcpy(scene_data, &scene_parameters_, sizeof(GpuSceneData));

  vmaUnmapMemory(allocator_, scene_parameters_buffer_.allocation);

  // Object data.
  void* object_data;
  vmaMapMemory(allocator_, GetFrame().object_buffer.allocation, &object_data);

  GpuObjectData* object_ssbo = reinterpret_cast<GpuObjectData*>(object_data);
  for (int i = 0; i < count; i++) {
    RenderObject& object = first[i];
    object_ssbo[i].model = object.transform;
  }

  vmaUnmapMemory(allocator_, GetFrame().object_buffer.allocation);

  Mesh* last_mesh = nullptr;
  Material* last_material = nullptr;

  for (int i = 0; i < count; i++) {
    RenderObject& object = first[i];
    assert(object.mesh);
    assert(object.material);
    // Only bind the pipeline if it doesn't match the one already bound.
    if (object.material != last_material) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        object.material->pipeline);
      last_material = object.material;

      uint32_t uniform_offset = buffer_offset;
      // Bind the descriptor set when changing pipelines. Because we only have
      // one dynamic offset, we only need to send 1 uniform offset.
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              object.material->pipeline_layout, 0, 1,
                              &GetFrame().global_descriptor, 1,
                              &uniform_offset);

      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              object.material->pipeline_layout, 1, 1,
                              &GetFrame().object_descriptor, 0, nullptr);
    }

    MeshPushConstants constants;
    constants.matrix = object.transform;
    vkCmdPushConstants(cmd, object.material->pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants),
                       &constants);

    const bool is_indexed_draw = !object.mesh->indices.empty();

    if (object.mesh != last_mesh) {
      VkDeviceSize offset = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->vertex_buffer.buffer,
                             &offset);
      if (is_indexed_draw) {
        vkCmdBindIndexBuffer(cmd, object.mesh->index_buffer.buffer, 0,
                             VK_INDEX_TYPE_UINT32);
      }
    }

    if (is_indexed_draw) {
      vkCmdDrawIndexed(cmd, static_cast<uint32_t>(object.mesh->indices.size()),
                       1, 0, 0, i);
    } else {
      vkCmdDraw(cmd, static_cast<uint32_t>(object.mesh->vertices.size()), 1, 0,
                i);
    }
  }
}

void Renderer::InitScene() {
  for (int i = 1; i <= 3; i++) {
    RenderObject shiba;
    shiba.mesh = GetMesh("shiba_" + std::to_string(i));
    shiba.material = GetMaterial("default");
    shiba.transform = glm::mat4{1.f};

    renderables_.push_back(shiba);
  }

  for (int x = -20; x <= 20; x++) {
    for (int z = -20; z <= 20; z++) {
      RenderObject triangle;
      triangle.mesh = GetMesh("triangle");
      triangle.material = GetMaterial("default");

      glm::mat4 translation =
          glm::translate(glm::mat4{1.f}, glm::vec3(x, 0, z));
      glm::mat4 scale = glm::scale(glm::mat4{1.f}, glm::vec3(.2f, .2f, .2f));
      triangle.transform = translation * scale;

      renderables_.push_back(triangle);
    }
  }
}

void Renderer::InitDescriptors() {
  // Create a descriptor pool that will hold 10 uniform buffers and 10 dynamic
  // uniform buffers.
  std::vector<VkDescriptorPoolSize> sizes = {
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
  };

  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.pNext = nullptr;

  pool_info.flags = 0;
  pool_info.maxSets = 10;
  pool_info.poolSizeCount = static_cast<uint32_t>(sizes.size());
  pool_info.pPoolSizes = sizes.data();

  vkCreateDescriptorPool(device_, &pool_info, nullptr, &descriptor_pool_);

  // Descriptor Set 1:

  // Binding for camera data at 0.
  VkDescriptorSetLayoutBinding camera_binding =
      init::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                       VK_SHADER_STAGE_VERTEX_BIT, 0);
  // Binding for scene data at 1.
  VkDescriptorSetLayoutBinding scene_binding = init::DescriptorSetLayoutBinding(
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1);

  VkDescriptorSetLayoutBinding bindings[] = {camera_binding, scene_binding};

  VkDescriptorSetLayoutCreateInfo descriptor_set_info = {};
  descriptor_set_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptor_set_info.pNext = nullptr;

  descriptor_set_info.flags = 0;
  descriptor_set_info.bindingCount = 2;
  descriptor_set_info.pBindings = bindings;

  vkCreateDescriptorSetLayout(device_, &descriptor_set_info, nullptr,
                              &global_set_layout_);

  // Descriptor Set 2:

  VkDescriptorSetLayoutBinding object_binding =
      init::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                       VK_SHADER_STAGE_VERTEX_BIT, 0);

  VkDescriptorSetLayoutCreateInfo descriptor_set_2_info = {};
  descriptor_set_2_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptor_set_2_info.pNext = nullptr;

  descriptor_set_2_info.flags = 0;
  descriptor_set_2_info.bindingCount = 1;
  descriptor_set_2_info.pBindings = &object_binding;

  vkCreateDescriptorSetLayout(device_, &descriptor_set_2_info, nullptr,
                              &object_set_layout_);

  // Scene buffer:
  const size_t scene_parameters_buffer_size =
      kFrameOverlap * GetAlignedBufferSize(sizeof(GpuSceneData));
  scene_parameters_buffer_ = CreateBuffer(
      allocator_, scene_parameters_buffer_size,
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
  deletion_stack_.Push([&]() {
    vmaDestroyBuffer(allocator_, scene_parameters_buffer_.buffer,
                     scene_parameters_buffer_.allocation);
  });

  for (int i = 0; i < kFrameOverlap; i++) {
    // Initialize object buffer.
    const int kMaxObjects = 10'000;
    frames_[i].object_buffer = CreateBuffer(
        allocator_, sizeof(GpuObjectData) * kMaxObjects,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    deletion_stack_.Push([&, i]() {
      vmaDestroyBuffer(allocator_, frames_[i].object_buffer.buffer,
                       frames_[i].object_buffer.allocation);
    });

    // Initialize camera buffer.
    frames_[i].camera_buffer = CreateBuffer(allocator_, sizeof(GpuCameraData),
                                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                            VMA_MEMORY_USAGE_CPU_TO_GPU);
    // Add buffers to the deletion stack.
    deletion_stack_.Push([&, i]() {
      vmaDestroyBuffer(allocator_, frames_[i].camera_buffer.buffer,
                       frames_[i].camera_buffer.allocation);
    });

    // Allocate one descriptor set for each frame.
    VkDescriptorSetAllocateInfo allocate_info = {};
    allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate_info.pNext = nullptr;

    allocate_info.descriptorPool = descriptor_pool_;
    allocate_info.descriptorSetCount = 1;
    allocate_info.pSetLayouts = &global_set_layout_;

    vkAllocateDescriptorSets(device_, &allocate_info,
                             &frames_[i].global_descriptor);

    // Allocate the descriptor set that will point to the object buffer.
    VkDescriptorSetAllocateInfo object_allocate_info = {};
    object_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    object_allocate_info.pNext = nullptr;

    object_allocate_info.descriptorPool = descriptor_pool_;
    object_allocate_info.descriptorSetCount = 1;
    object_allocate_info.pSetLayouts = &object_set_layout_;

    vkAllocateDescriptorSets(device_, &object_allocate_info,
                             &frames_[i].object_descriptor);

    VkDescriptorBufferInfo camera_info = {};
    camera_info.buffer = frames_[i].camera_buffer.buffer;
    camera_info.offset = 0;
    camera_info.range = sizeof(GpuCameraData);

    VkDescriptorBufferInfo scene_info = {};
    scene_info.buffer = scene_parameters_buffer_.buffer;
    scene_info.offset = 0;  // We're using a dynamic buffer.
    scene_info.range = sizeof(GpuSceneData);

    VkDescriptorBufferInfo object_info = {};
    object_info.buffer = frames_[i].object_buffer.buffer;
    object_info.offset = 0;
    object_info.range = sizeof(GpuObjectData) * kMaxObjects;

    VkWriteDescriptorSet camera_write =
        init::WriteDescriptorSet(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                 frames_[i].global_descriptor, &camera_info, 0);

    VkWriteDescriptorSet scene_write =
        init::WriteDescriptorSet(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                                 frames_[i].global_descriptor, &scene_info, 1);

    VkWriteDescriptorSet object_write =
        init::WriteDescriptorSet(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                 frames_[i].object_descriptor, &object_info, 0);

    VkWriteDescriptorSet set_writes[] = {camera_write, scene_write,
                                         object_write};

    vkUpdateDescriptorSets(device_, 3, set_writes, 0, nullptr);
  }

  deletion_stack_.Push([&]() {
    vkDestroyDescriptorSetLayout(device_, global_set_layout_, nullptr);
    vkDestroyDescriptorSetLayout(device_, object_set_layout_, nullptr);
    vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
  });
}

Renderer::FrameData& Renderer::GetFrame() {
  return frames_[framenumber_ % kFrameOverlap];
}

size_t Renderer::GetAlignedBufferSize(size_t original_size) {
  // Calculate the required alignment based on minimum device offset alignment.
  size_t min_alignment = gpu_properties_.limits.minUniformBufferOffsetAlignment;
  size_t aligned_size = original_size;

  if (min_alignment > 0) {
    // See:
    // https://github.com/SaschaWillems/Vulkan/tree/master/examples/dynamicuniformbuffer
    aligned_size = (aligned_size + min_alignment - 1) & ~(min_alignment - 1);
  }

  return aligned_size;
}

};  // namespace vk
