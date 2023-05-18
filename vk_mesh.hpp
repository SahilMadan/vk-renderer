#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <array>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vector>

#include "buffer.hpp"
#include "queue_submitter.hpp"
#include "texture.hpp"
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
  std::vector<uint32_t> indices;
  AllocatedBuffer vertex_buffer;
  AllocatedBuffer index_buffer;
};

struct MeshPushConstants {
  glm::vec4 data;
  glm::mat4 matrix;
};

struct Model {
  std::vector<Mesh> meshes;
  std::vector<Texture> textures;
};

Model LoadFromFile(const char* filename, VmaAllocator allocator,
                   VkDevice device, QueueSubmitter& queue_submitter);

}  // namespace vk
