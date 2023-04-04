#include "shader.hpp"

#include <fstream>
#include <vector>

namespace vk {

bool LoadShader(const VkDevice& device, const char* file_path,
                VkShaderModule* out_shader) {
  std::ifstream file(file_path, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  size_t file_size = static_cast<size_t>(file.tellg());

  std::vector<uint32_t> buffer(file_size / sizeof(uint32_t));

  file.seekg(0);
  file.read(reinterpret_cast<char*>(buffer.data()), file_size);
  file.close();

  VkShaderModuleCreateInfo shader_info = {};
  shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_info.pNext = nullptr;

  shader_info.codeSize = buffer.size() * sizeof(uint32_t);
  shader_info.pCode = buffer.data();

  VkShaderModule shader;
  if (vkCreateShaderModule(device, &shader_info, nullptr, &shader) !=
      VK_SUCCESS) {
    return false;
  }

  *out_shader = shader;
  return true;
}

}  // namespace vk