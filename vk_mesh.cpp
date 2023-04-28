#include "vk_mesh.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NOEXCEPTION
#include <tiny_gltf.h>

namespace {

void LoadNode(tinygltf::Model& model, tinygltf::Node& node,
              std::vector<vk::Mesh>& meshes) {
  for (int n : node.children) {
    LoadNode(model, model.nodes[n], meshes);
  }
  if (node.mesh == -1) {
    return;
  }

  const tinygltf::Mesh mesh = model.meshes[node.mesh];
  vk::Mesh new_mesh;

  // NOTE: We aren't loading models correctly yet. For instance, we are not
  // considering primitive type and are instead stuffing all models directly
  // into one mesh.
  for (size_t i = 0; i < mesh.primitives.size(); i++) {
    const tinygltf::Primitive& primitive = mesh.primitives[i];

    // Position:
    const tinygltf::Accessor& position_accessor =
        model.accessors[primitive.attributes.find("POSITION")->second];
    const tinygltf::BufferView& position_buffer_view =
        model.bufferViews[position_accessor.bufferView];

    const float* position_buffer = reinterpret_cast<const float*>(
        &(model.buffers[position_buffer_view.buffer]
              .data[position_accessor.byteOffset +
                    position_buffer_view.byteOffset]));

    uint32_t vertex_count = static_cast<uint32_t>(position_accessor.count);
    uint32_t position_stride =
        position_accessor.ByteStride(position_buffer_view)
            ? (position_accessor.ByteStride(position_buffer_view) /
               sizeof(float))
            : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);

    // Normal:
    // TODO: Handle the case where the model has no normals.
    const tinygltf::Accessor& normal_accessor =
        model.accessors[primitive.attributes.find("NORMAL")->second];
    const tinygltf::BufferView& normal_buffer_view =
        model.bufferViews[normal_accessor.bufferView];

    const float* normal_buffer = reinterpret_cast<const float*>(
        (&model.buffers[normal_buffer_view.buffer]
              .data[normal_accessor.byteOffset +
                    normal_buffer_view.byteOffset]));

    uint32_t normal_stride =
        normal_accessor.ByteStride(normal_buffer_view)
            ? (normal_accessor.ByteStride(normal_buffer_view) / sizeof(float))
            : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);

    for (size_t v = 0; v < vertex_count; v++) {
      vk::Vertex vertex;
      vertex.position = glm::make_vec3(&position_buffer[v * position_stride]);
      vertex.normal = glm::make_vec3(&normal_buffer[v * normal_stride]);
      vertex.color = vertex.normal;

      new_mesh.vertices.push_back(std::move(vertex));
    }

    const tinygltf::Accessor& index_accessor =
        model.accessors[primitive.indices > -1 ? primitive.indices : 0];
    const tinygltf::BufferView& index_buffer_view =
        model.bufferViews[index_accessor.bufferView];

    const void* index_buffer_ptr =
        &(model.buffers[index_buffer_view.buffer]
              .data[index_accessor.byteOffset + index_buffer_view.byteOffset]);

    size_t index_count = static_cast<uint32_t>(index_accessor.count);

    switch (index_accessor.componentType) {
      case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
        const uint32_t* index_buffer =
            static_cast<const uint32_t*>(index_buffer_ptr);
        for (size_t i = 0; i < index_count; i++) {
          new_mesh.indices.push_back(index_buffer[i]);
        }
      } break;
      case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
        const uint16_t* index_buffer =
            static_cast<const uint16_t*>(index_buffer_ptr);
        for (size_t i = 0; i < index_count; i++) {
          new_mesh.indices.push_back(index_buffer[i]);
        }
      } break;
      case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
        const uint8_t* index_buffer =
            static_cast<const uint8_t*>(index_buffer_ptr);
        for (size_t i = 0; i < index_count; i++) {
          new_mesh.indices.push_back(index_buffer[i]);
        }
      } break;
    }
  }

  meshes.push_back(new_mesh);
}

}  // namespace

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

std::vector<Mesh> LoadFromFile(const char* filename) {
  tinygltf::TinyGLTF loader;
  tinygltf::Model model;

  std::string error;
  std::string warning;

  bool result = loader.LoadASCIIFromFile(&model, &error, &warning, filename);
  if (!warning.empty()) {
    std::cout << "warning loading model: " << warning << std::endl;
  }
  if (!error.empty()) {
    std::cerr << "error loading model: " << error << std::endl;
  }
  if (!result) {
    return {};
  }

  const tinygltf::Scene& scene =
      model.scenes[model.defaultScene > -1 ? model.defaultScene : 0];
  tinygltf::Node& node = model.nodes[scene.nodes[0]];

  std::vector<Mesh> meshes;

  LoadNode(model, node, meshes);

  return meshes;
}

}  // namespace vk