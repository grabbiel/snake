#include "modelloader.hpp"
#include "gltfloader.hpp"
#include "objloader.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

bool fileExists(const char *path) {
  std::ifstream file(path);
  return file.good();
}

// Get file extension
std::string getFileExtension(const char *filename) {
  std::string path = filename;
  size_t extPos = path.find_last_of('.');
  if (extPos != std::string::npos) {
    return path.substr(extPos);
  }
  return "";
}

bool loadModelBase(const char *filename, std::vector<glm::vec3> &out_vertices,
                   std::vector<glm::vec2> &out_uvs,
                   std::vector<glm::vec3> &out_normals,
                   std::vector<unsigned int> &out_indices) {

  // Check if file exists
  if (!fileExists(filename)) {
    std::cerr << "Error: File doesn't exist: " << filename << std::endl;
    return false;
  }

  // Get file extension
  std::string ext = getFileExtension(filename);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  // Load based on file extension
  if (ext == ".gltf" || ext == ".glb") {
    // Use tinygltf for glTF files
    return loadGLTFModel(filename, out_vertices, out_uvs, out_normals,
                         out_indices);
  } else if (ext == ".obj") {
    // Use OBJ loader and convert to indexed format
    bool result = loadAssimpModel(filename, out_vertices, out_uvs, out_normals);

    // If OBJ loader doesn't generate indices, create sequential indices
    if (result && out_indices.empty()) {
      for (size_t i = 0; i < out_vertices.size(); i++) {
        out_indices.push_back(static_cast<unsigned int>(i));
      }
    }

    return result;
  } else {
    std::cerr << "Unsupported file format: " << ext << std::endl;
    std::cerr << "Supported formats: .obj, .gltf, .glb" << std::endl;
    return false;
  }
}
