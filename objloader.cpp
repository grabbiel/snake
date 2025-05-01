#include "objloader.hpp"

#ifndef __EMSCRIPTEN__
// Original Assimp-based loader for desktop
bool loadAssimpModel(const char *path, std::vector<glm::vec3> &out_vertices,
                     std::vector<glm::vec2> &out_uvs,
                     std::vector<glm::vec3> &out_normals) {

  Assimp::Importer importer;
  const aiScene *scene =
      importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenNormals |
                                  aiProcess_CalcTangentSpace);

  if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE ||
      !scene->mRootNode) {
    std::cerr << "Assimp error: " << importer.GetErrorString() << std::endl;
    return false;
  }

  if (scene->mNumMeshes < 1) {
    std::cerr << "No meshes found in file" << std::endl;
    return false;
  }

  const aiMesh *mesh = scene->mMeshes[0];

  // Vertices
  out_vertices.reserve(mesh->mNumVertices);
  for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
    glm::vec3 vertex;
    vertex.x = mesh->mVertices[i].x;
    vertex.y = mesh->mVertices[i].y;
    vertex.z = mesh->mVertices[i].z;
    out_vertices.push_back(vertex);
  }

  // UVs
  if (mesh->mTextureCoords[0]) {
    out_uvs.reserve(mesh->mNumVertices);
    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
      glm::vec2 uv;
      uv.x = mesh->mTextureCoords[0][i].x;
      uv.y = mesh->mTextureCoords[0][i].y;
      out_uvs.push_back(uv);
    }
  }

  // Normals
  if (mesh->mNormals) {
    out_normals.reserve(mesh->mNumVertices);
    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
      glm::vec3 normal;
      normal.x = mesh->mNormals[i].x;
      normal.y = mesh->mNormals[i].y;
      normal.z = mesh->mNormals[i].z;
      out_normals.push_back(normal);
    }
  }

  return true;
}
#else
// WebGL-compatible OBJ loader for Emscripten
bool loadAssimpModel(const char *path, std::vector<glm::vec3> &out_vertices,
                     std::vector<glm::vec2> &out_uvs,
                     std::vector<glm::vec3> &out_normals) {
  return false;
}
#endif
