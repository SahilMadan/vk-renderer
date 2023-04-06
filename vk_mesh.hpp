#pragma once

#include <array>
#include <glm/vec3.hpp>
#include <vector>

#include "vk_types.hpp"

namespace vk {

struct VertexInputDescription {
  std::array<VkVertexInputBindingDescription, 1> bindings;
  std::array<VkVertexInputAttributeDescription, 3> attributes;

  VkPipelineVertexInputStateCreateFlags flags = 0;
};

struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec3 color;

  static VertexInputDescription GetDescription();
};

struct Mesh {
  std::vector<Vertex> vertices;
  AllocatedBuffer buffer;
};

}  // namespace vk
