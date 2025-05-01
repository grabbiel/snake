#pragma once

#include "glm/common.hpp"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef __EMSCRIPTEN__
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#endif

// Main model loading function - implementation differs for native vs WebGL
bool loadAssimpModel(const char *path, std::vector<glm::vec3> &out_vertices,
                     std::vector<glm::vec2> &out_uvs,
                     std::vector<glm::vec3> &out_normals);

// Helper functions that may be useful for both implementations
#ifdef __EMSCRIPTEN__
#endif
