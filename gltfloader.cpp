#include "gltfloader.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

// Define these before including tiny_gltf to set up the implementation
// correctly
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NOEXCEPTION
#define JSON_NOEXCEPTION
#include "deps/tiny_gltf/tiny_gltf.h"

bool loadGLTFModel(const char *filename, std::vector<glm::vec3> &out_vertices,
                   std::vector<glm::vec2> &out_uvs,
                   std::vector<glm::vec3> &out_normals,
                   std::vector<unsigned int> &out_indices) {

  // Clear output vectors
  out_vertices.clear();
  out_uvs.clear();
  out_normals.clear();
  out_indices.clear();

  std::cout << "Loading glTF file: " << filename << std::endl;

  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  std::string err;
  std::string warn;

  bool binary = false;
  std::string ext = std::string(filename);
  size_t extPos = ext.find_last_of('.');
  if (extPos != std::string::npos) {
    ext = ext.substr(extPos);
    if (ext == ".glb") {
      binary = true;
    }
  }

  bool ret = false;
  if (binary) {
    ret = loader.LoadBinaryFromFile(&model, &err, &warn, filename);
  } else {
    ret = loader.LoadASCIIFromFile(&model, &err, &warn, filename);
  }

  if (!warn.empty()) {
    std::cout << "glTF warning: " << warn << std::endl;
  }

  if (!err.empty()) {
    std::cerr << "glTF error: " << err << std::endl;
  }

  if (!ret) {
    std::cerr << "Failed to load glTF file: " << filename << std::endl;
    return false;
  }

  // Check for meshes in the model
  if (model.meshes.empty()) {
    std::cerr << "No meshes found in glTF file" << std::endl;
    return false;
  }

  // For simplicity, we'll process the first mesh only
  const tinygltf::Mesh &mesh = model.meshes[0];

  // Process primitives in the mesh
  for (size_t p = 0; p < mesh.primitives.size(); p++) {
    const tinygltf::Primitive &primitive = mesh.primitives[p];

    // Get indices
    if (primitive.indices >= 0) {
      const tinygltf::Accessor &accessor = model.accessors[primitive.indices];
      const tinygltf::BufferView &bufferView =
          model.bufferViews[accessor.bufferView];
      const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

      const unsigned char *data =
          &buffer.data[bufferView.byteOffset + accessor.byteOffset];
      const size_t count = accessor.count;

      // Adjusting index offset to account for multiple primitives
      const uint32_t vertexOffset = static_cast<uint32_t>(out_vertices.size());

      // Extract indices based on component type
      if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        const uint16_t *indices = reinterpret_cast<const uint16_t *>(data);
        for (size_t i = 0; i < count; i++) {
          out_indices.push_back(static_cast<unsigned int>(indices[i]) +
                                vertexOffset);
        }
      } else if (accessor.componentType ==
                 TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
        const uint32_t *indices = reinterpret_cast<const uint32_t *>(data);
        for (size_t i = 0; i < count; i++) {
          out_indices.push_back(indices[i] + vertexOffset);
        }
      } else if (accessor.componentType ==
                 TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        const uint8_t *indices = reinterpret_cast<const uint8_t *>(data);
        for (size_t i = 0; i < count; i++) {
          out_indices.push_back(static_cast<unsigned int>(indices[i]) +
                                vertexOffset);
        }
      } else {
        std::cerr << "Unsupported index component type" << std::endl;
        return false;
      }
    }

    // Get position data
    auto positionIt = primitive.attributes.find("POSITION");
    if (positionIt != primitive.attributes.end()) {
      const tinygltf::Accessor &accessor = model.accessors[positionIt->second];
      const tinygltf::BufferView &bufferView =
          model.bufferViews[accessor.bufferView];
      const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

      const unsigned char *data =
          &buffer.data[bufferView.byteOffset + accessor.byteOffset];
      const size_t byteStride = accessor.ByteStride(bufferView);
      const size_t count = accessor.count;

      // Reserve space
      out_vertices.reserve(out_vertices.size() + count);

      // Get min/max for normalization (if needed)
      glm::vec3 min, max;
      if (!accessor.minValues.empty() && !accessor.maxValues.empty()) {
        min = glm::vec3(accessor.minValues[0], accessor.minValues[1],
                        accessor.minValues[2]);
        max = glm::vec3(accessor.maxValues[0], accessor.maxValues[1],
                        accessor.maxValues[2]);
      }

      // Extract vertices
      for (size_t i = 0; i < count; i++) {
        const float *pos =
            reinterpret_cast<const float *>(data + i * byteStride);
        out_vertices.push_back(glm::vec3(pos[0], pos[1], pos[2]));
      }
    }

    // Get normal data
    auto normalIt = primitive.attributes.find("NORMAL");
    if (normalIt != primitive.attributes.end()) {
      const tinygltf::Accessor &accessor = model.accessors[normalIt->second];
      const tinygltf::BufferView &bufferView =
          model.bufferViews[accessor.bufferView];
      const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

      const unsigned char *data =
          &buffer.data[bufferView.byteOffset + accessor.byteOffset];
      const size_t byteStride = accessor.ByteStride(bufferView);
      const size_t count = accessor.count;

      // Reserve space
      out_normals.reserve(out_normals.size() + count);

      // Extract normals
      for (size_t i = 0; i < count; i++) {
        const float *norm =
            reinterpret_cast<const float *>(data + i * byteStride);
        out_normals.push_back(glm::vec3(norm[0], norm[1], norm[2]));
      }
    }

    // Get texcoord data
    auto texcoordIt = primitive.attributes.find("TEXCOORD_0");
    if (texcoordIt != primitive.attributes.end()) {
      const tinygltf::Accessor &accessor = model.accessors[texcoordIt->second];
      const tinygltf::BufferView &bufferView =
          model.bufferViews[accessor.bufferView];
      const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

      const unsigned char *data =
          &buffer.data[bufferView.byteOffset + accessor.byteOffset];
      const size_t byteStride = accessor.ByteStride(bufferView);
      const size_t count = accessor.count;

      // Reserve space
      out_uvs.reserve(out_uvs.size() + count);

      // Extract texture coordinates
      for (size_t i = 0; i < count; i++) {
        const float *uv =
            reinterpret_cast<const float *>(data + i * byteStride);
        out_uvs.push_back(glm::vec2(uv[0], uv[1]));
      }
    }
  }

  // If we don't have indices but have vertices, create sequential indices
  if (out_indices.empty() && !out_vertices.empty()) {
    for (size_t i = 0; i < out_vertices.size(); i++) {
      out_indices.push_back(static_cast<unsigned int>(i));
    }
  }

  // Generate default normals if they don't exist
  if (out_normals.empty() && !out_vertices.empty() && !out_indices.empty()) {
    out_normals.resize(out_vertices.size(), glm::vec3(0.0f));

    // Calculate normals for each triangle
    for (size_t i = 0; i < out_indices.size(); i += 3) {
      if (i + 2 < out_indices.size()) {
        unsigned int i1 = out_indices[i];
        unsigned int i2 = out_indices[i + 1];
        unsigned int i3 = out_indices[i + 2];

        if (i1 < out_vertices.size() && i2 < out_vertices.size() &&
            i3 < out_vertices.size()) {
          glm::vec3 v1 = out_vertices[i1];
          glm::vec3 v2 = out_vertices[i2];
          glm::vec3 v3 = out_vertices[i3];

          glm::vec3 edge1 = v2 - v1;
          glm::vec3 edge2 = v3 - v1;

          glm::vec3 normal = glm::cross(edge1, edge2);
          if (glm::length(normal) > 0.00001f) {
            normal = glm::normalize(normal);
          }

          out_normals[i1] += normal;
          out_normals[i2] += normal;
          out_normals[i3] += normal;
        }
      }
    }

    // Normalize all normals
    for (auto &normal : out_normals) {
      if (glm::length(normal) > 0.00001f) {
        normal = glm::normalize(normal);
      } else {
        normal = glm::vec3(0.0f, 1.0f, 0.0f); // Default up vector
      }
    }
  }

  // Generate default UVs if they don't exist
  if (out_uvs.empty() && !out_vertices.empty()) {
    // Simple planar mapping
    for (const auto &v : out_vertices) {
      out_uvs.push_back(glm::vec2((v.x + 1.0f) * 0.5f, (v.z + 1.0f) * 0.5f));
    }
  }

  std::cout << "Loaded glTF model: " << out_vertices.size() << " vertices, "
            << out_normals.size() << " normals, " << out_uvs.size() << " UVs, "
            << out_indices.size() << " indices" << std::endl;

  return !out_vertices.empty();
}
