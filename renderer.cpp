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

constexpr uint64_t kTimeoutNanoSecs = 1000000000;

#ifdef _DEBUG
constexpr bool kEnableValidationLayers = true;
#else
constexpr bool kEnableValidationLayers = false;
#endif

const std::vector<const char*> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation"};

const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME};

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
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(device, &features);
    if (!features.geometryShader) continue;
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
  return std::make_optional<SelectedDeviceDetails>(
      {devices[max_index], max_queue_family, std::move(max_swapchain_details)});
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
  VkCommandPoolCreateInfo command_pool_info = {};
  command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool_info.pNext = nullptr;

  command_pool_info.queueFamilyIndex = graphics_queue_family_;
  command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  if (vkCreateCommandPool(device_, &command_pool_info, nullptr,
                          &command_pool_) != VK_SUCCESS) {
    return false;
  }
  deletion_stack_.Push(
      [=]() { vkDestroyCommandPool(device_, command_pool_, nullptr); });

  VkCommandBufferAllocateInfo allocate_info = {};
  allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocate_info.pNext = nullptr;
  allocate_info.commandPool = command_pool_;
  allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocate_info.commandBufferCount = 1;

  if (vkAllocateCommandBuffers(device_, &allocate_info, &command_buffer_) !=
      VK_SUCCESS) {
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
  VkFenceCreateInfo fence_info = {};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.pNext = nullptr;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  if (vkCreateFence(device_, &fence_info, nullptr, &render_fence_) !=
      VK_SUCCESS) {
    return false;
  }
  deletion_stack_.Push(
      [=]() { vkDestroyFence(device_, render_fence_, nullptr); });

  VkSemaphoreCreateInfo semaphore_info = {};
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphore_info.pNext = nullptr;
  semaphore_info.flags = 0;

  if (vkCreateSemaphore(device_, &semaphore_info, nullptr,
                        &render_semaphore_)) {
    return false;
  }
  deletion_stack_.Push(
      [=]() { vkDestroySemaphore(device_, render_semaphore_, nullptr); });

  if (vkCreateSemaphore(device_, &semaphore_info, nullptr,
                        &present_semaphore_)) {
    return false;
  }
  deletion_stack_.Push(
      [=]() { vkDestroySemaphore(device_, present_semaphore_, nullptr); });

  if (!InitPipeline()) {
    return false;
  }

  if (!LoadMeshes()) {
    return false;
  }

  // Everything is initialized.
  initialized_ = true;
}

void Renderer::Shutdown() {
  if (vkGetFenceStatus(device_, render_fence_) &&
      vkWaitForFences(device_, 1, &render_fence_, true, kTimeoutNanoSecs) !=
          VK_SUCCESS) {
    return;
  }
  deletion_stack_.Flush();
}

void Renderer::Draw() {
  // TODO: Handle errors gracefully.

  // Wait until the GPU has finished rendering the last frame.
  if (vkWaitForFences(device_, 1, &render_fence_, true, kTimeoutNanoSecs) !=
      VK_SUCCESS) {
    return;
  }
  if (vkResetFences(device_, 1, &render_fence_) != VK_SUCCESS) {
    return;
  }

  // Request an image from the swapchain.
  uint32_t swapchain_image_index;
  if (vkAcquireNextImageKHR(device_, swapchain_, kTimeoutNanoSecs,
                            present_semaphore_, nullptr,
                            &swapchain_image_index) != VK_SUCCESS) {
    return;
  }

  // Now we can safely reset the command buffer.
  if (vkResetCommandBuffer(command_buffer_, 0) != VK_SUCCESS) {
    return;
  }

  VkCommandBufferBeginInfo begin_info = {};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.pNext = nullptr;
  begin_info.pInheritanceInfo = nullptr;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  if (vkBeginCommandBuffer(command_buffer_, &begin_info) != VK_SUCCESS) {
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

  vkCmdBeginRenderPass(command_buffer_, &renderpass_info,
                       VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    mesh_pipeline_);
  VkDeviceSize offset = 0;

  // Push constants.
  glm::vec3 camera_position = {0.f, 0.f, -2.0f};
  glm::mat4 view = glm::translate(glm::mat4(1.f), camera_position);
  glm::mat4 projection =
      glm::perspective(glm::radians(70.f), 1700.f / 900.f, 0.1f, 200.f);
  projection[1][1] *= -1;
  glm::mat4 model = glm::rotate(
      glm::mat4(1.f), glm::radians(framenumber_ * 0.4f), glm::vec3(0, 1, 0));
  glm::mat4 mesh_matrix = projection * view * model;

  MeshPushConstants constants;
  constants.matrix = mesh_matrix;

  vkCmdPushConstants(command_buffer_, mesh_pipeline_layout_,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants),
                     &constants);

  for (Mesh& m : shiba_mesh_) {
    vkCmdBindVertexBuffers(command_buffer_, 0, 1, &m.vertex_buffer.buffer,
                           &offset);
    vkCmdBindIndexBuffer(command_buffer_, m.index_buffer.buffer, 0,
                         VK_INDEX_TYPE_UINT32);

    int vertex_count = m.vertices.size();

    // vkCmdDraw(command_buffer_, vertex_count, 1, 0, 0);
    vkCmdDrawIndexed(command_buffer_, static_cast<uint32_t>(m.indices.size()),
                     1, 0, 0, 0);
  }

  vkCmdEndRenderPass(command_buffer_);
  if (vkEndCommandBuffer(command_buffer_) != VK_SUCCESS) {
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
  submit.pWaitSemaphores = &present_semaphore_;

  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &render_semaphore_;

  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &command_buffer_;

  if (vkQueueSubmit(graphics_queue_, 1, &submit, render_fence_) != VK_SUCCESS) {
    return;
  }

  VkPresentInfoKHR present_info = {};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.pNext = nullptr;

  present_info.swapchainCount = 1;
  present_info.pSwapchains = &swapchain_;

  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = &render_semaphore_;

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
  if (!LoadShader(device_, "shaders/colored_triangle.frag.spv", &mesh_frag)) {
    std::cerr << "Unable to load file: colored_triangle.frag.spv" << std::endl;
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

  vkDestroyShaderModule(device_, mesh_vert, nullptr);
  vkDestroyShaderModule(device_, mesh_frag, nullptr);

  return true;
}

bool Renderer::LoadMeshes() {
  shiba_mesh_ = LoadFromFile("assets/models/shiba/scene.gltf");
  if (shiba_mesh_.empty()) {
    return false;
  }

  for (Mesh& m : shiba_mesh_) {
    UploadMesh(m);
  }

  return true;
}

bool Renderer::UploadMesh(Mesh& mesh) {
  // Allocate vertex buffer.
  VkBufferCreateInfo buffer_info = {};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.pNext = nullptr;

  buffer_info.size = mesh.vertices.size() * sizeof(Vertex);
  buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

  VmaAllocationCreateInfo allocation_info = {};
  allocation_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

  if (vmaCreateBuffer(allocator_, &buffer_info, &allocation_info,
                      &mesh.vertex_buffer.buffer,
                      &mesh.vertex_buffer.allocation, nullptr) != VK_SUCCESS) {
    return false;
  }

  deletion_stack_.Push([=]() {
    vmaDestroyBuffer(allocator_, mesh.vertex_buffer.buffer,
                     mesh.vertex_buffer.allocation);
  });

  // Copy vertex data.
  void* data;
  vmaMapMemory(allocator_, mesh.vertex_buffer.allocation, &data);
  memcpy(data, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));
  vmaUnmapMemory(allocator_, mesh.vertex_buffer.allocation);

  if (mesh.indices.empty()) {
    return true;
  }

  VkBufferCreateInfo index_buffer_info = {};
  index_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  index_buffer_info.pNext = nullptr;

  index_buffer_info.size = mesh.indices.size() * sizeof(uint32_t);
  index_buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

  if (vmaCreateBuffer(allocator_, &index_buffer_info, &allocation_info,
                      &mesh.index_buffer.buffer, &mesh.index_buffer.allocation,
                      nullptr) != VK_SUCCESS) {
    return false;
  }
  deletion_stack_.Push([=]() {
    vmaDestroyBuffer(allocator_, mesh.index_buffer.buffer,
                     mesh.index_buffer.allocation);
  });

  vmaMapMemory(allocator_, mesh.index_buffer.allocation, &data);
  memcpy(data, mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));
  vmaUnmapMemory(allocator_, mesh.index_buffer.allocation);

  return true;
}

};  // namespace vk
