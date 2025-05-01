#!/bin/bash

# Native build script similar to compile_web.sh
set -e

# Set up directories
BUILD_DIR=build-native
mkdir -p $BUILD_DIR
mkdir -p assets/models

# Set default model
DEFAULT_OBJ="assets/models/snakeH.obj"
DEFAULT_GLTF="assets/models/snakeH.gltf"

# Convert OBJ to GLTF if needed
if [ -f "$DEFAULT_OBJ" ] && [ ! -f "$DEFAULT_GLTF" ]; then
  echo "Converting $DEFAULT_OBJ to $DEFAULT_GLTF"
  ./build/obj2gltf "$DEFAULT_OBJ" "$DEFAULT_GLTF"
fi

if [ -f "$DEFAULT_GLTF" ]; then
  DEFAULT_MODEL="snakeH.gltf"
else
  echo "No valid model found. Cannot build native renderer."
  exit 1
fi

echo "Using model: $DEFAULT_MODEL"

# Detect GLM
GLM_INCLUDE_DIR=""
GLM_PATHS=(
  "/opt/homebrew/include"
  "/usr/local/include"
  "/usr/include"
  "./deps/glm"
)
for path in "${GLM_PATHS[@]}"; do
  if [ -f "$path/glm/glm.hpp" ]; then
    GLM_INCLUDE_DIR="$path"
    echo "Found GLM at: $GLM_INCLUDE_DIR"
    break
  fi
done

if [ -z "$GLM_INCLUDE_DIR" ]; then
  echo "GLM not found. Please install it or place it in ./deps/glm"
  exit 1
fi

# Detect GLFW
GLFW_INCLUDE_DIR=""
GLFW_LIB_DIR=""
GLFW_PATHS=(
  "/opt/homebrew/include"
  "/usr/local/include"
  "/usr/include"
)
for path in "${GLFW_PATHS[@]}"; do
  if [ -f "$path/GLFW/glfw3.h" ]; then
    GLFW_INCLUDE_DIR="$path"
    GLFW_LIB_DIR="${path%/include}/lib"
    echo "Found GLFW at: $GLFW_INCLUDE_DIR"
    break
  fi
done

if [ -z "$GLFW_INCLUDE_DIR" ]; then
  echo "GLFW not found. Please install it via Homebrew or your package manager."
  exit 1
fi

# Assimp paths
ASSIMP_INCLUDE_DIR="/opt/homebrew/opt/assimp/include"
ASSIMP_LIB_DIR="/opt/homebrew/opt/assimp/lib"

# Compiler flags
CXX=g++
CXXFLAGS="-std=c++11 -Wall -Wextra -I$ASSIMP_INCLUDE_DIR -I$GLM_INCLUDE_DIR -I$GLFW_INCLUDE_DIR -Ideps"
LDFLAGS="-L$ASSIMP_LIB_DIR -lassimp -L$GLFW_LIB_DIR -lglfw -framework OpenGL"
SRC="simple-cube.cpp objloader.cpp gltfloader.cpp modelloader.cpp"
OUT="$BUILD_DIR/3d-renderer"

# Compile
echo "Compiling native renderer..."
$CXX $CXXFLAGS $SRC -o $OUT $LDFLAGS

echo "✅ Native build complete: $OUT"
echo "Run it with: ./$OUT"
