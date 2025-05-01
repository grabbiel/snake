// Snake.cpp
#include "Snake.hpp"
#include "deform.hpp"
#include "modelloader.hpp"
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

Snake::Snake()
    : speed(5.0f), started(false), pendingGrowth(0), alive(true),
      colorTint(1.0f, 1.0f, 1.0f), currentVertexBuffer(0),
      spineControlPointsChanged(true), distanceSinceLastTurn(0.0f),
      distanceSinceLastSample(0.0f), bodyRadius(0.0f) {
  // Initialize buffers to 0
  vertexBuffers[0] = 0;
  vertexBuffers[1] = 0;
  normalBuffer = 0;
  uvBuffer = 0;
  indexBuffer = 0;

  // Initialize direction
  direction = glm::vec3(1.0f, 0.0f, 0.0f);
  nextDirection = direction;
}

Snake::~Snake() {
  // Clean up OpenGL resources
  if (vertexBuffers[0])
    glDeleteBuffers(1, &vertexBuffers[0]);
  if (vertexBuffers[1])
    glDeleteBuffers(1, &vertexBuffers[1]);
  if (normalBuffer)
    glDeleteBuffers(1, &normalBuffer);
  if (uvBuffer)
    glDeleteBuffers(1, &uvBuffer);
  if (indexBuffer)
    glDeleteBuffers(1, &indexBuffer);
}

bool Snake::loadModel(const char *filename) {
  printf("Loading snake model: %s\n", filename);

  // Clear existing data
  vertices.clear();
  uvs.clear();
  normals.clear();
  indices.clear();

  // Load the model
  bool result = loadModelBase(filename, vertices, uvs, normals, indices);

  if (!result || vertices.empty()) {
    printf("Failed to load snake model. Using default cube.\n");
    // Create default cube vertices here as fallback
    // This would be the code from createDefaultCube()
    return false;
  }

  printf("Successfully loaded snake model with %zu vertices, %zu indices\n",
         vertices.size(), indices.size());

  // Initialize buffers
  updateBuffers();

  // Calculate vertex arc lengths and initialize spine
  calculateVertexArcLengths();
  initializeSpine();

  // Bind vertices to spine
  deformedVertices.resize(vertices.size());
  vertexBindings.resize(vertices.size());
  bindVerticesToSpine(spine, vertices);

  return true;
}

void Snake::initialize(const glm::vec3 &startPos, const glm::vec3 &startDir) {
  position = startPos;
  direction = glm::normalize(startDir);
  nextDirection = direction;

  // Reset state
  started = false;
  alive = true;
  pendingGrowth = 0;
  spineControlPointsChanged = true;

  // Re-initialize spine at the new position
  initializeSpine();
}

void Snake::setColorTint(const glm::vec3 &color) { colorTint = color; }

glm::vec3 Snake::getColorTint() const { return colorTint; }

void Snake::update(float deltaTime) {
  if (!started || !alive || spine.getControlPoints().empty()) {
    return;
  }

  // Get current control points
  std::vector<glm::vec3> controlPoints = spine.getControlPoints();

  // Apply pending growth first
  while (pendingGrowth > 0) {
    if (controlPoints.size() < 2)
      break; // Safety check

    glm::vec3 tail = controlPoints.back();
    glm::vec3 secondLast = controlPoints[controlPoints.size() - 2];
    glm::vec3 tailDir = glm::normalize(tail - secondLast);

    glm::vec3 newPoint =
        tail + tailDir * (-1.0f); // Use a standard spacing value
    controlPoints.push_back(newPoint);

    pendingGrowth--;
  }

  // Move head position
  controlPoints[0] += nextDirection * speed * deltaTime;

  // Update snake body
  for (size_t i = 1; i < controlPoints.size(); i++) {
    glm::vec3 direction = controlPoints[i - 1] - controlPoints[i];
    float distance = glm::length(direction);

    if (distance > 1.0f) { // Use a standard spacing value
      controlPoints[i] += glm::normalize(direction) * (distance - 1.0f);
    }
  }

  // Update spine
  spine.clear();
  for (const auto &point : controlPoints) {
    spine.addControlPoint(point);
  }

  spineControlPointsChanged = true;

  // Deform mesh to follow spine
  deformMeshToSpline();
}

void Snake::handleInput(int forwardKey, int backKey, int leftKey,
                        int rightKey) {
  // Implementation will need to use GLFW
  // This would check if keys are pressed and update nextDirection
}

void Snake::grow(int segments) { pendingGrowth += segments; }

bool Snake::checkSelfCollision() const {
  // Implementation based on existing collision detection
  const float collisionRadius = bodyRadius * 0.8f;

  auto controlPoints = spine.getControlPoints();
  if (controlPoints.size() < 5)
    return false;

  for (size_t i = 4; i < controlPoints.size(); ++i) {
    float dist = glm::distance(controlPoints[0], controlPoints[i]);
    if (dist < 2.0f * collisionRadius) {
      return true;
    }
  }

  return false;
}

bool Snake::checkCollisionWithPoint(const glm::vec3 &point,
                                    float radius) const {
  // Check if a point collides with any part of the snake
  auto controlPoints = spine.getControlPoints();

  for (const auto &cp : controlPoints) {
    if (glm::distance(point, cp) < (bodyRadius + radius)) {
      return true;
    }
  }

  return false;
}

bool Snake::checkCollisionWithSnake(const Snake &other) const {
  // Check if this snake collides with another snake
  auto myHead = spine.getControlPoints()[0];

  // Implementation depends on how we want to define snake-to-snake collision
  return other.checkCollisionWithPoint(myHead, bodyRadius);
}

bool Snake::isAlive() const { return alive; }

void Snake::render(GLuint shaderProgram, const glm::mat4 &viewMatrix,
                   const glm::mat4 &projectionMatrix) {
  if (vertices.empty()) {
    return;
  }

  // Use the provided shader program
  glUseProgram(shaderProgram);

  // Set model matrix (just at origin for now)
  glm::mat4 modelMatrix = glm::mat4(1.0f);

  // Calculate MVP matrix
  glm::mat4 mvpMatrix = projectionMatrix * viewMatrix * modelMatrix;

  // Set uniform values
  GLuint mvpLocation = glGetUniformLocation(shaderProgram, "MVP");
  GLuint modelMatrixLocation =
      glGetUniformLocation(shaderProgram, "modelMatrix");
  GLuint colorLocation = glGetUniformLocation(shaderProgram, "modelColor");

  glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, glm::value_ptr(mvpMatrix));
  glUniformMatrix4fv(modelMatrixLocation, 1, GL_FALSE,
                     glm::value_ptr(modelMatrix));
  glUniform3fv(colorLocation, 1, glm::value_ptr(colorTint));

  // Set up vertex attributes
  GLuint posLocation = glGetAttribLocation(shaderProgram, "vertexPosition");
  GLuint normalLocation = glGetAttribLocation(shaderProgram, "vertexNormal");
  GLuint uvLocation = glGetAttribLocation(shaderProgram, "vertexUV");

  // Bind vertex position buffer (use current buffer from double buffering)
  glBindBuffer(GL_ARRAY_BUFFER, vertexBuffers[currentVertexBuffer]);
  glVertexAttribPointer(posLocation, 3, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(posLocation);

  // Bind normal buffer
  glBindBuffer(GL_ARRAY_BUFFER, normalBuffer);
  glVertexAttribPointer(normalLocation, 3, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(normalLocation);

  // Bind UV buffer
  glBindBuffer(GL_ARRAY_BUFFER, uvBuffer);
  glVertexAttribPointer(uvLocation, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(uvLocation);

  // Bind index buffer
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);

  // Draw elements
  glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);

  // Clean up
  glDisableVertexAttribArray(posLocation);
  glDisableVertexAttribArray(normalLocation);
  glDisableVertexAttribArray(uvLocation);
}

glm::vec3 Snake::getHeadPosition() const {
  if (spine.getControlPoints().empty()) {
    return position;
  }
  return spine.getControlPoints()[0];
}

glm::vec3 Snake::getDirection() const { return direction; }

void Snake::initializeSpine() {
  // Clear any existing control points
  spine.clear();

  // Calculate model bounds
  glm::vec3 min(FLT_MAX), max(-FLT_MAX);
  for (const auto &v : vertices) {
    min = glm::min(min, v);
    max = glm::max(max, v);
  }

  // Calculate snake body radius
  glm::vec3 extents = max - min;

  int primaryAxis = 0;
  float maxExtent = extents.x;
  if (extents.y > maxExtent) {
    primaryAxis = 1;
    maxExtent = extents.y;
  }
  if (extents.z > maxExtent) {
    primaryAxis = 2;
    maxExtent = extents.z;
  }

  float minorExtent1, minorExtent2;
  if (primaryAxis == 0) {
    minorExtent1 = extents.y;
    minorExtent2 = extents.z;
  } else if (primaryAxis == 1) {
    minorExtent1 = extents.x;
    minorExtent2 = extents.z;
  } else {
    minorExtent1 = extents.x;
    minorExtent2 = extents.y;
  }

  float bodyThickness = (minorExtent1 + minorExtent2) * 0.5f;
  bodyRadius = bodyThickness * 0.5f;

  // Create initial control points
  const int numPoints = 20;
  float modelLength = maxExtent;
  float spacing = modelLength / (numPoints - 1);

  for (int i = 0; i < numPoints; i++) {
    float t = static_cast<float>(i) / (numPoints - 1);

    // Adjust positions based on the primary axis
    glm::vec3 point = position;

    // Calculate offset based on direction and spacing
    glm::vec3 offset =
        direction * spacing * (static_cast<float>(numPoints) - 1 - i);
    point -= offset; // Points go backward from head position

    spine.addControlPoint(point);
  }

  spineControlPointsChanged = true;
  cachedSpineFrames.clear();
}

void Snake::calculateVertexArcLengths() {
  // Implementation from original calculateVertexArcLengths function
  // ...
}

void Snake::updateBuffers() {
  // Create/update vertex buffers
  if (vertexBuffers[0] == 0) {
    glGenBuffers(2, vertexBuffers);
  }

  for (int i = 0; i < 2; i++) {
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffers[i]);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3),
                 vertices.data(), GL_DYNAMIC_DRAW);
  }

  // Create/update normal buffer
  if (normalBuffer == 0) {
    glGenBuffers(1, &normalBuffer);
  }
  glBindBuffer(GL_ARRAY_BUFFER, normalBuffer);
  glBufferData(GL_ARRAY_BUFFER, normals.size() * sizeof(glm::vec3),
               normals.data(), GL_STATIC_DRAW);

  // Create/update UV buffer
  if (uvBuffer == 0) {
    glGenBuffers(1, &uvBuffer);
  }
  glBindBuffer(GL_ARRAY_BUFFER, uvBuffer);
  glBufferData(GL_ARRAY_BUFFER, uvs.size() * sizeof(glm::vec2), uvs.data(),
               GL_STATIC_DRAW);

  // Create/update index buffer
  if (indexBuffer == 0) {
    glGenBuffers(1, &indexBuffer);
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);

  // Reset current buffer
  currentVertexBuffer = 0;
}

void Snake::deformMeshToSpline() {
  // This should call into the deform utility function, but with local variables
  ::deformMeshToSpline(vertices, deformedVertices, spine);

  // Update the vertex buffer with deformed vertices
  int nextBuffer = (currentVertexBuffer + 1) % 2;
  glBindBuffer(GL_ARRAY_BUFFER, vertexBuffers[nextBuffer]);
  glBufferData(GL_ARRAY_BUFFER, deformedVertices.size() * sizeof(glm::vec3),
               deformedVertices.data(), GL_DYNAMIC_DRAW);

  // Switch to the updated buffer
  currentVertexBuffer = nextBuffer;
}
