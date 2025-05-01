#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

// Unified model loader that handles both OBJ and glTF formats
bool loadModelBase(const char *filename, std::vector<glm::vec3> &out_vertices,
                   std::vector<glm::vec2> &out_uvs,
                   std::vector<glm::vec3> &out_normals,
                   std::vector<unsigned int> &out_indices);
