#include "renderer.hpp"

#include <iostream>
#include <optional>

namespace {

#ifdef _DEBUG
constexpr bool kEnableValidationLayers = true;
#else
constexpr bool kEnableValidationLayers = false;
#endif

const std::vector<const char*> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation"};

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

struct SelectedDevice {
  VkPhysicalDevice device;
  uint32_t graphics_queue_family;
};

std::optional<uint32_t> GetQueueFamilyIndices(VkPhysicalDevice device, VkSurfaceKHR& surface) {
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

std::optional<SelectedDevice> SelectDevice(
    std::vector<VkPhysicalDevice>& devices, VkSurfaceKHR& surface) {
  std::vector<int> scores(devices.size());
  int max_score = 0;
  int max_index = -1;
  uint32_t max_queue_family = 0;
  for (int i = 0; i < devices.size(); i++) {
    VkPhysicalDevice device = devices[i];
    std::optional<uint32_t> queue_family = GetQueueFamilyIndices(device, surface);
    // Get the graphics queue. If there isn't one, this device isn't supported.
    if (!queue_family.has_value()) {
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
    }
  }
  if (max_index < 0) {
    // Unable to find a suitable device.
    return std::nullopt;
  }
  return std::make_optional<SelectedDevice>(
      {devices[max_index], max_queue_family});
}

}  // namespace

namespace vk {

bool Renderer::Init(InitParams params) {
  render_extent_.width = params.width;
  render_extent_.height = params.height;

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
  std::optional<SelectedDevice> selected_device = SelectDevice(devices, surface_);
  if (!selected_device.has_value()) {
    return false;
  }
  gpu_ = selected_device.value().device;

  // Initialize the device queue.
  VkDeviceQueueCreateInfo queue_info = {};
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_info.pNext = nullptr;
  queue_info.queueFamilyIndex = selected_device.value().graphics_queue_family;
  queue_info.queueCount = 1;
  float queue_priority = 1.0f;  // Highest priority since we only have 1.
  queue_info.pQueuePriorities = &queue_priority;
  graphics_queue_family_ = selected_device.value().graphics_queue_family;

  // Initialize the logical device.
  VkPhysicalDeviceFeatures device_features = {};
  device_features.geometryShader = VK_TRUE;

  VkDeviceCreateInfo device_info = {};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.pNext = nullptr;

  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  device_info.pEnabledFeatures = &device_features;

  if (kEnableValidationLayers) {
    device_info.enabledLayerCount =
        static_cast<uint32_t>(kValidationLayers.size());
    device_info.ppEnabledLayerNames = kValidationLayers.data();
  } else {
    device_info.enabledLayerCount = 0;
  }

  if (vkCreateDevice(gpu_, &device_info, nullptr, &device_) != VK_SUCCESS) {
    std::cerr << "Cannot create device!\n" << std::endl;
    return false;
  }
  deletion_stack_.Push(
      [&]() { vkDestroyDevice(device_, nullptr); });
  

  // Initialize the graphics queue.
  vkGetDeviceQueue(device_, selected_device.value().graphics_queue_family, 0,
                   &graphics_queue_);

  initialized_ = true;
}

void Renderer::Shutdown() { deletion_stack_.Flush(); }

};  // namespace vk
