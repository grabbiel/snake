#pragma once

#include "glm/vec3.hpp"
#include "spline.hpp"
#include <vector>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten.h>
#define USING_GLES
#else
#include <OpenGL/gl3.h>
#endif

struct UniformCache {
  glm::mat4 lastMVP;
  glm::mat4 lastModelMatrix;
  bool initialized = false;
};

struct FoodItem {
  glm::vec3 position;   // Position on the grid
  float size;           // Size of the food item
  glm::vec3 color;      // Base color
  float glowIntensity;  // How much it glows
  float animationPhase; // For pulsing animation
};

extern float gridSize;
extern std::vector<float> vertexArcLengths; // One float per vertex
extern glm::vec3 snakePosition;
extern glm::vec3 snakeDirection;
extern glm::vec3 nextDirection;
extern float snakeSpeed;
extern float distanceSinceLastTurn;
extern float distanceSinceLastSample;
extern Spline snakeSpine;
extern std::vector<glm::vec3> deformedVertices;
extern bool snakeStarted;
extern float segmentSpacing; // Distance between segments
extern std::vector<VertexBinding> vertexBindings;
extern GLuint vertex_buffers[2];
extern int current_vertex_buffer;
extern bool spineControlPointsChanged;               // Dirty flag for spine
extern std::vector<SplineFrame> cachedSpineFrames;   // Cached frames
extern std::vector<glm::vec3> previousControlPoints; // For dirty checking
extern GLuint cached_program_id;
extern std::vector<glm::vec3> gridVertices;
extern std::vector<glm::vec3> gridColors;
extern GLuint grid_vertex_buffer;
extern GLuint grid_color_buffer;
extern GLuint grid_vao;
extern float gridScale;
extern int mapGridSize;
extern std::vector<FoodItem> foodItems;
extern int maxFoodItems;          // Maximum number of food items
extern float foodSpawnTimer;      // Timer for spawning new food
extern float foodSpawnInterval;   // How often to spawn new food
extern GLuint food_vertex_buffer; // Vertex buffer for food rendering
extern GLuint food_glow_buffer;   // Buffer for glow parameters
extern int pendingGrowth;
extern bool gameOver;
extern float snakeBodyRadius;
