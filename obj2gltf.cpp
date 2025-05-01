#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// For native builds, use Assimp for conversion
#ifndef __EMSCRIPTEN__
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

// Convert an OBJ file to glTF
bool convertObjToGltf(const std::string &inputFile,
                      const std::string &outputFile) {
  std::cout << "Converting " << inputFile << " to " << outputFile << std::endl;

  // Import the OBJ file
  Assimp::Importer importer;
  const aiScene *scene = importer.ReadFile(
      inputFile, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                     aiProcess_CalcTangentSpace |
                     aiProcess_JoinIdenticalVertices |
                     aiProcess_PreTransformVertices);

  if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE ||
      !scene->mRootNode) {
    std::cerr << "Assimp error: " << importer.GetErrorString() << std::endl;
    return false;
  }

  // Export to glTF
  Assimp::Exporter exporter;
  aiReturn ret = exporter.Export(scene, "gltf2", outputFile);

  if (ret != AI_SUCCESS) {
    std::cerr << "Assimp export error: " << exporter.GetErrorString()
              << std::endl;
    return false;
  }

  std::cout << "Successfully converted to " << outputFile << std::endl;
  return true;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "Usage: " << argv[0] << " <obj_file> [output_file]"
              << std::endl;
    std::cout << "If output_file is not specified, will use the same name with "
                 ".gltf extension"
              << std::endl;
    return 1;
  }

  std::string inputFile = argv[1];
  std::string outputFile;

  if (argc >= 3) {
    outputFile = argv[2];
  } else {
    // Replace extension with .gltf
    outputFile = inputFile.substr(0, inputFile.find_last_of('.')) + ".gltf";
  }

  if (convertObjToGltf(inputFile, outputFile)) {
    return 0;
  } else {
    return 1;
  }
}
#else
// For Emscripten builds, just stub out the executable
int main(int argc, char **argv) {
  std::cout << "This utility is not meant to be run in Emscripten."
            << std::endl;
  return 1;
}
#endif
