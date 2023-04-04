#pragma once

#include <vulkan/vulkan.h>

namespace vk {

bool LoadShader(const VkDevice& device, const char* file_path,
                VkShaderModule* out_shader);

}  // namespace vk
