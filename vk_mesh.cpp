#include "vk_mesh.hpp"

namespace vk {

VertexInputDescription Vertex::GetDescription() {
  static bool init = false;
  static VertexInputDescription description;
  if (!init) {
    // We have just 1 vertex buffer binding, with a per-vertex rate.
    VkVertexInputBindingDescription input_binding = {};
    input_binding.binding = 0;
    input_binding.stride = sizeof(Vertex);
    input_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    description.bindings[0] = input_binding;

    // Position is stored at location = 0.
    VkVertexInputAttributeDescription position = {};
    position.binding = 0;
    position.location = 0;
    position.format = VK_FORMAT_R32G32B32_SFLOAT;
    position.offset = offsetof(Vertex, position);
    description.attributes[0] = position;

    // Normal is stored at location = 1.
    VkVertexInputAttributeDescription normal = {};
    normal.binding = 0;
    normal.location = 1;
    normal.format = VK_FORMAT_R32G32B32_SFLOAT;
    normal.offset = offsetof(Vertex, normal);
    description.attributes[1] = normal;

    // Color is stored at location = 2.
    VkVertexInputAttributeDescription color = {};
    color.binding = 0;
    color.location = 2;
    color.format = VK_FORMAT_R32G32B32_SFLOAT;
    color.offset = offsetof(Vertex, color);
    description.attributes[2] = color;

    init = true;
  }

  return description;
}

}  // namespace vk