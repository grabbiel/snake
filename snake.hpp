// Snake.hpp
#pragma once

#include "spline.hpp"
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/em_asm.h>
#include <emscripten/html5.h>
#define USING_GLES
#else
#include <OpenGL/gl3.h>
#endif
#include <glm/glm.hpp>
#include <string>
#include <vector>

class Snake {
private:
  // Model data
  std::vector<glm::vec3> vertices;
  std::vector<glm::vec2> uvs;
  std::vector<glm::vec3> normals;
  std::vector<unsigned int> indices;

  // Spline for movement
  Spline spine;
  std::vector<glm::vec3> deformedVertices;
  std::vector<VertexBinding> vertexBindings;
  std::vector<SplineFrame> cachedSpineFrames;
  bool spineControlPointsChanged;
  std::vector<glm::vec3> previousControlPoints;

  // Movement properties
  glm::vec3 position;
  glm::vec3 direction;
  glm::vec3 nextDirection;
  float speed;
  float distanceSinceLastTurn;
  float distanceSinceLastSample;
  bool started;
  int pendingGrowth;
  bool alive;

  // Visual properties
  glm::vec3 colorTint;

  // Rendering data
  GLuint vertexBuffers[2];
  int currentVertexBuffer;
  GLuint normalBuffer;
  GLuint uvBuffer;
  GLuint indexBuffer;

  // Calculated properties
  float bodyRadius;

public:
  Snake();
  ~Snake();

  // Initialization
  bool loadModel(const char *filename);
  void initialize(const glm::vec3 &startPos, const glm::vec3 &startDir);

  // Customization
  void setColorTint(const glm::vec3 &color);
  glm::vec3 getColorTint() const;

  // Game logic
  void update(float deltaTime);
  void handleInput(int forwardKey, int backKey, int leftKey, int rightKey);
  void grow(int segments);
  bool checkSelfCollision() const;
  bool checkCollisionWithPoint(const glm::vec3 &point, float radius) const;
  bool checkCollisionWithSnake(const Snake &other) const;
  bool isAlive() const;

  // Rendering
  void render(GLuint shaderProgram, const glm::mat4 &viewMatrix,
              const glm::mat4 &projectionMatrix);

  // Getters
  glm::vec3 getHeadPosition() const;
  glm::vec3 getDirection() const;

  // Helper methods
  void initializeSpine();
  void calculateVertexArcLengths();
  void updateBuffers();
  void deformMeshToSpline();
};
