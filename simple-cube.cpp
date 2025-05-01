#include "globals.hpp"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Handle platform-specific includes
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/em_asm.h>
#include <emscripten/html5.h>
#define USING_GLES
#else
#include <OpenGL/gl3.h>
#endif

// GLM includes for math operations
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

// Include our model loader
#include "modelloader.hpp"

// Include our model loader and spline deformation
#include "deform.hpp"
#include "modelloader.hpp"
#include "spline.hpp"

// Global variables
GLFWwindow *g_window = nullptr;
int g_window_width = 1280;
int g_window_height = 720;
float lastTime = 0.0f;
float deltaTime = 0.0f;
bool modelLoaded = false;
std::string currentModelFile = "";
UniformCache uniformCache;

// Shader program
GLuint program_id = 0;
GLuint mvp_location = 0;
GLuint model_matrix_location;
GLuint vertex_position_location;
GLuint vertex_normal_location;
GLuint vertex_uv_location;
GLuint normal_buffer = 0;
GLuint uv_buffer = 0;
GLuint vertex_buffers[2] = {0, 0};
GLfloat *vertexAttribPointers[2] = {nullptr, nullptr};
int current_vertex_buffer = 0;
GLuint index_buffer = 0;
GLuint vao = 0;
GLuint cached_program_id = 0;

// Food Shader
GLuint food_program_id = 0;
GLuint food_mvp_location = 0;
GLuint food_time_location = 0;
GLuint food_color_location = 0;

// Model data
std::vector<glm::vec3> vertices;
std::vector<glm::vec2> uvs;
std::vector<glm::vec3> normals;
std::vector<unsigned int> indices;
float snakeBodyRadius = 0.0f;

// Deformed vertex buffer (used with spline spine)
std::vector<VertexBinding> vertexBindings;
std::vector<glm::vec3> deformedVertices;
std::vector<glm::vec3> lastDeformedVertices; // Previous frame vertices
std::vector<bool> vertexChanged;             // Tracks which vertices changed
std::vector<unsigned int> changedIndices;    // Indices of changed vertices
float vertexChangeThreshold = 0.0001f;       // Square distance threshold
bool firstDeformation = true;                // Flag for first deformation
//
// Spline spine
Spline snakeSpine;
int segmentCount = 20;               // Number of segments (tail length)
float segmentSpacing = 0.5f;         // Distance between segments
std::vector<float> vertexArcLengths; // One float per vertex
int pendingGrowth = 0;

// Snake movement
bool snakeStarted = false;
glm::vec3 snakePosition(0.0f);
glm::vec3 snakeDirection(0.0f, 0.0f, -1.0f); // initially moving along X
glm::vec3 nextDirection = snakeDirection;    // Store desired direction
float snakeSpeed = 5.0f;                     // units per second
float distanceSinceLastTurn =
    0.0f; // How much the snake has traveled since last direction change
float distanceSinceLastSample = 0.0f;
bool spineControlPointsChanged = true; // Start dirty
std::vector<SplineFrame> cachedSpineFrames;
std::vector<glm::vec3> previousControlPoints;

// Grid
float gridSize = 1.0f;
std::vector<glm::vec3> gridVertices;
std::vector<glm::vec3> gridColors;
GLuint grid_vertex_buffer = 0;
GLuint grid_color_buffer = 0;
GLuint grid_vao = 0;
float gridScale = 1.0f; // Size of each grid cell
int mapGridSize = 20;   // Number of cells (half-width)

// Camera and transformation matrices
glm::mat4 model_matrix = glm::mat4(1.0f);
glm::mat4 view_matrix;
glm::mat4 projection_matrix;
glm::mat4 mvp_matrix;

// Food
std::vector<FoodItem> foodItems;
int maxFoodItems = 10;          // Maximum number of food items
float foodSpawnTimer = 0.0f;    // Timer for spawning new food
float foodSpawnInterval = 2.0f; // Spawn new food every 2 seconds
GLuint food_vertex_buffer = 0;  // Vertex buffer for food rendering
GLuint food_glow_buffer = 0;    // Buffer for glow parameters
int foodsEaten = 0;

// Game State
float cameraHeight = 60.0f; // y position of your camera (static for now)
bool gameOver = false;
float fovY = 45.0f; // Your camera's field of view in degrees
float cameraDistance =
    glm::length(glm::vec3(0.0f, 60.0f, 0.0f)); // From your camera position

// WebGL Persistent Memory
#ifdef __EMSCRIPTEN__
// For tracking WebAssembly memory
std::vector<float> vertexDataBuffer; // Use std::vector instead of raw pointer
#endif

// Vertex shader source
const char *vertex_shader_source = R"(
    attribute vec3 vertexPosition;
    attribute vec3 vertexNormal;
    attribute vec2 vertexUV;
    attribute vec3 vertexColor;  // Add color attribute
    
    uniform mat4 MVP;
    uniform mat4 modelMatrix;
    
    varying vec3 fragmentNormal;
    varying vec2 fragmentUV;
    varying vec3 fragmentPosition;
    varying vec3 fragmentColor;  // Add color varying
    
    void main() {
        gl_Position = MVP * vec4(vertexPosition, 1.0);
        
        // Transform normal to world space
        fragmentNormal = (modelMatrix * vec4(vertexNormal, 0.0)).xyz;
        
        // Pass through UV coordinates
        fragmentUV = vertexUV;
        
        // Calculate world space position
        fragmentPosition = (modelMatrix * vec4(vertexPosition, 1.0)).xyz;
        
        // Pass color to fragment shader
        fragmentColor = vertexColor;
    }
)";

// Optimized fragment shader with early returns and efficient calculations
const char *fragment_shader_source = R"(
    varying vec3 fragmentNormal;
    varying vec2 fragmentUV;
    varying vec3 fragmentPosition;
    varying vec3 fragmentColor;  // Add color varying
    
    uniform float opacity;  // Add opacity uniform
    
    void main() {
        // Check if color is set (non-zero)
        bool hasColor = (fragmentColor.r > 0.0 || fragmentColor.g > 0.0 || fragmentColor.b > 0.0);
        
        if (hasColor) {
            // Use vertex color directly for grid lines, with the opacity uniform
            gl_FragColor = vec4(fragmentColor, opacity);
        } else {
            // Basic diffuse lighting for model
            vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
            float diffuse = max(dot(normalize(fragmentNormal), lightDir), 0.1);
            
            // Create a pattern from UVs for visualization
            vec3 baseColor = vec3(fragmentUV.x, fragmentUV.y, 1.0 - fragmentUV.x);
            
            // Generate final color with lighting
            vec3 color = baseColor * diffuse;
            
            gl_FragColor = vec4(color, 1.0);
        }
    }
)";

// Food vertex shader with glow effect
const char *food_vertex_shader_source = R"(
    precision mediump float;
    
    attribute vec3 position;
    attribute float glowFactor;
    
    uniform mat4 MVP;
    uniform float time;
    
    varying float vGlowFactor;
    varying vec3 vPosition;
    varying float vPulseFactor;
    
    void main() {
        // Calculate pulse factor but DON'T apply it to position
        float pulseFactor = 1.0 + 0.2 * sin(time * 2.0);
        
        // Keep position unchanged
        gl_Position = MVP * vec4(position, 1.0);
        
        // Only apply pulsing to the point size
        gl_PointSize = 15.0 + 10.0 * glowFactor * pulseFactor;
        
        // Pass values to fragment shader
        vGlowFactor = glowFactor;
        vPosition = position;
        vPulseFactor = pulseFactor; // Pass pulse factor to fragment shader
    }
)";

// Food fragment shader with glow effect
const char *food_fragment_shader_source = R"(
    precision mediump float;
    
    uniform vec3 foodColor;
    uniform float time;
    
    varying float vGlowFactor;
    varying vec3 vPosition;
    varying float vPulseFactor;
    
    void main() {
        // Calculate distance from center of point sprite
        vec2 center = vec2(0.5, 0.5);
        vec2 coord = gl_PointCoord - center;
        float dist = length(coord);
        
        // Create circular point with soft edge
        float alpha = 1.0 - smoothstep(0.3, 0.5, dist);
        
        // Glow pulse effect - more pronounced
        float glowPulse = 0.5 + 0.5 * sin(time * 3.0 + vPosition.x + vPosition.z);
        
        // Enhanced glow
        vec3 baseColor = foodColor;
        vec3 glowColor = vec3(1.0, 1.0, 0.7) * 2.0; // Brighter yellow glow
        
        // More dramatic mixing of color based on glow
        vec3 finalColor = mix(baseColor, glowColor, vGlowFactor * glowPulse * vPulseFactor);
        
        // Additional brightness around the edges for glow effect
        float edgeGlow = 1.0 - smoothstep(0.3, 0.5, dist);
        finalColor += glowColor * vGlowFactor * glowPulse * edgeGlow * 0.5;
        
        // Make center brighter
        float centerBrightness = 1.0 + vGlowFactor * 2.0 * vPulseFactor * (1.0 - dist * 2.0);
        finalColor *= centerBrightness;
        
        gl_FragColor = vec4(finalColor, alpha);
    }
)";

// Function to compile shader
GLuint compile_shader(GLenum type, const char *source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    GLint info_log_length;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);
    std::vector<char> info_log(info_log_length + 1);
    glGetShaderInfoLog(shader, info_log_length, nullptr, &info_log[0]);
    printf("Shader compilation error: %s\n", &info_log[0]);
    return 0;
  }

  return shader;
}

GLuint create_food_program() {
  // Compile shaders
  GLuint vertex_shader =
      compile_shader(GL_VERTEX_SHADER, food_vertex_shader_source);
  GLuint fragment_shader =
      compile_shader(GL_FRAGMENT_SHADER, food_fragment_shader_source);

  if (!vertex_shader || !fragment_shader) {
    printf("Failed to compile food shaders.\n");
    return 0;
  }

  // Create and link program
  GLuint program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);

  // Bind attribute locations
  glBindAttribLocation(program, 0, "position");
  glBindAttribLocation(program, 1, "glowFactor");

  glLinkProgram(program);

  // Check for linking errors
  GLint success;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (!success) {
    GLint info_log_length;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_length);
    std::vector<char> info_log(info_log_length + 1);
    glGetProgramInfoLog(program, info_log_length, nullptr, &info_log[0]);
    printf("Food program linking error: %s\n", &info_log[0]);
    return 0;
  }

  // Clean up shaders
  glDetachShader(program, vertex_shader);
  glDetachShader(program, fragment_shader);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  return program;
}

// Function to create shader program
GLuint create_program() {
  // Set the appropriate preamble based on platform
  std::string vertex_preamble, fragment_preamble;

#ifdef USING_GLES
  vertex_preamble = "#version 100\n"; // OpenGL ES 2.0
  fragment_preamble =
      "#version 100\nprecision mediump float;\n"; // Required for GLES
#else
  vertex_preamble = "#version 120\n";
  fragment_preamble = "#version 120\n";
#endif

  // Combine preamble with shader source
  std::string vertex_full = vertex_preamble + vertex_shader_source;
  std::string fragment_full = fragment_preamble + fragment_shader_source;

  // Compile shaders
  GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_full.c_str());
  GLuint fragment_shader =
      compile_shader(GL_FRAGMENT_SHADER, fragment_full.c_str());

  if (!vertex_shader || !fragment_shader) {
    printf("Failed to compile shaders.\n");
    return 0;
  }

  // Create and link program
  GLuint program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glLinkProgram(program);

  // Check for linking errors
  GLint success;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (!success) {
    GLint info_log_length;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_length);
    std::vector<char> info_log(info_log_length + 1);
    glGetProgramInfoLog(program, info_log_length, nullptr, &info_log[0]);
    printf("Program linking error: %s\n", &info_log[0]);
    return 0;
  }

  // Clean up shaders
  glDetachShader(program, vertex_shader);
  glDetachShader(program, fragment_shader);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  return program;
}

// Create grid vertices
void createGrid() {
  int width, height;
  glfwGetFramebufferSize(g_window, &width, &height);
  float aspectRatio = (float)width / (float)height;

  // Calculate visible area at camera's target plane
  float visibleHeight = 2.0f * cameraDistance * tan(glm::radians(fovY * 0.5f));
  float visibleWidth = visibleHeight * aspectRatio;

  // Add 20% margin to ensure grid covers entire viewport
  float margin = 1.0f;
  mapGridSize = (int)std::ceil(std::max(visibleWidth, visibleHeight) * margin /
                               (2.0f * gridScale));

  // Rest of your grid creation code...
  gridVertices.clear();
  gridColors.clear();

  float gridExtent = mapGridSize * gridScale;

  // Create grid lines along X and Z axes
  for (int i = -mapGridSize; i <= mapGridSize; i++) {
    float pos = i * gridScale;

    // Lines along X-axis (vary Z)
    gridVertices.push_back(glm::vec3(-gridExtent, 0.0f, pos));
    gridVertices.push_back(glm::vec3(gridExtent, 0.0f, pos));

    // Lines along Z-axis (vary X)
    gridVertices.push_back(glm::vec3(pos, 0.0f, -gridExtent));
    gridVertices.push_back(glm::vec3(pos, 0.0f, gridExtent));

    // Colors - thin lines for regular grid
    glm::vec3 lineColor =
        (i == 0) ? glm::vec3(0.0f, 0.7f, 0.0f) : // Center axes (green)
            glm::vec3(0.4f, 0.4f, 0.4f);         // Regular grid (gray)

    // Add colors for both pairs of vertices
    gridColors.push_back(lineColor);
    gridColors.push_back(lineColor);
    gridColors.push_back(lineColor);
    gridColors.push_back(lineColor);
  }

  // Add border lines with increased thickness and different color
  // Bottom border (min Z)
  gridVertices.push_back(glm::vec3(-gridExtent, 0.0f, -gridExtent));
  gridVertices.push_back(glm::vec3(gridExtent, 0.0f, -gridExtent));

  // Top border (max Z)
  gridVertices.push_back(glm::vec3(-gridExtent, 0.0f, gridExtent));
  gridVertices.push_back(glm::vec3(gridExtent, 0.0f, gridExtent));

  // Left border (min X)
  gridVertices.push_back(glm::vec3(-gridExtent, 0.0f, -gridExtent));
  gridVertices.push_back(glm::vec3(-gridExtent, 0.0f, gridExtent));

  // Right border (max X)
  gridVertices.push_back(glm::vec3(gridExtent, 0.0f, -gridExtent));
  gridVertices.push_back(glm::vec3(gridExtent, 0.0f, gridExtent));

  // Red color for all border lines
  glm::vec3 borderColor(0.8f, 0.1f, 0.1f); // Red
  for (int i = 0; i < 8; i++) {
    gridColors.push_back(borderColor);
  }

  printf("Created grid with %zu vertices\n", gridVertices.size());
}

// Spawn a new food item with a more dramatic color
void spawnFood() {
  FoodItem food;

  // Corrected: use camera parameters to calculate visible ground area
  float fovY = 45.0f; // vertical field of view in degrees
  float aspectRatio = (float)g_window_width / (float)g_window_height;

  // Calculate half-extent of visible ground rectangle
  float halfHeight = cameraHeight * tan(glm::radians(fovY * 0.5f));
  float halfWidth = halfHeight * aspectRatio;

  // Set spawn attempts limit
  int attempts = 0;
  const int MAX_ATTEMPTS = 25;
  bool validPosition = false;

  while (!validPosition && attempts < MAX_ATTEMPTS) {
    // Random X and Z within ground view bounds
    food.position.x = (((float)rand() / RAND_MAX) * 2.0f - 1.0f) * halfWidth;
    food.position.z = (((float)rand() / RAND_MAX) * 2.0f - 1.0f) * halfHeight;
    food.position.y = 0.2f; // Always a little above the ground

    // Check for collisions with existing food
    bool tooClose = false;
    for (const auto &existingFood : foodItems) {
      if (glm::distance(food.position, existingFood.position) <
          2.0f * gridScale) {
        tooClose = true;
        break;
      }
    }

    if (!tooClose) {
      validPosition = true;
    } else {
      attempts++;
    }
  }

  if (!validPosition) {
    printf("Warning: Failed to find valid food position after %d attempts\n",
           MAX_ATTEMPTS);
    // Still place it at a random fallback
  }

  // Assign color randomly
  const glm::vec3 vibrantColors[] = {
      glm::vec3(1.0f, 0.2f, 0.2f), // Red
      glm::vec3(0.2f, 1.0f, 0.2f), // Green
      glm::vec3(0.2f, 0.2f, 1.0f), // Blue
      glm::vec3(1.0f, 1.0f, 0.2f), // Yellow
      glm::vec3(1.0f, 0.2f, 1.0f), // Magenta
      glm::vec3(0.2f, 1.0f, 1.0f), // Cyan
      glm::vec3(1.0f, 0.6f, 0.1f), // Orange
      glm::vec3(0.7f, 0.2f, 1.0f)  // Purple
  };

  int colorIndex = rand() % 8;
  food.color = vibrantColors[colorIndex];

  // Randomize glow intensity and size
  food.size = 0.8f + (float)rand() / RAND_MAX * 0.5f;
  food.glowIntensity = 0.7f + (float)rand() / RAND_MAX * 0.6f;
  food.animationPhase =
      (float)rand() / RAND_MAX * 6.28f; // Random phase [0, 2π]

  foodItems.push_back(food);
}
// Initialize food rendering resources
void initializeFood() {
  // Create shader program
  food_program_id = create_food_program();
  if (!food_program_id) {
    printf("Failed to create food shader program\n");
    return;
  }

  // Get uniform locations
  food_mvp_location = glGetUniformLocation(food_program_id, "MVP");
  food_time_location = glGetUniformLocation(food_program_id, "time");
  food_color_location = glGetUniformLocation(food_program_id, "foodColor");

  // Create buffers
  if (food_vertex_buffer == 0) {
    glGenBuffers(1, &food_vertex_buffer);
  }
  if (food_glow_buffer == 0) {
    glGenBuffers(1, &food_glow_buffer);
  }

  // Check for WebGL point sprite support - FIXED VERSION
#ifdef __EMSCRIPTEN__
  // Log WebGL capabilities using Module.ctx
  EM_ASM({
    console.log("WebGL: Checking capabilities");
    // Use Module.ctx which is the pre-established WebGL context
    if (Module.ctx) {
      try {
        console.log(
            "Point size range: " +
            Module.ctx.getParameter(Module.ctx.ALIASED_POINT_SIZE_RANGE));
        console.log("Using standard WebGL point sprites");
      } catch (e) {
        console.log("Error checking WebGL capabilities: " + e);
      }
    } else {
      console.log("WebGL context not available in Module.ctx");
    }
  });
#endif

  // Spawn initial food
  while (foodItems.size() < maxFoodItems) {
    spawnFood();
  }
}

void growSnake() {
  std::vector<glm::vec3> controlPoints = snakeSpine.getControlPoints();

  if (controlPoints.size() < 2) {
    return;
  }

  size_t lastIndex = controlPoints.size() - 1;
  const glm::vec3 &lastPoint = controlPoints[lastIndex];
  const glm::vec3 &secondLastPoint = controlPoints[lastIndex - 1];

  glm::vec3 direction = lastPoint - secondLastPoint;

  if (glm::length(direction) > 0.001f) {
    direction = glm::normalize(direction);
  } else {
    direction = glm::vec3(0.0f, 0.0f, 1.0f); // default
  }

  // Calculate spacing
  float segmentLength = (controlPoints.size() > 1)
                            ? glm::distance(controlPoints[0], controlPoints[1])
                            : 1.0f; // fallback

  glm::vec3 newPoint = lastPoint + direction * segmentLength;

  snakeSpine.addControlPoint(newPoint);
}

// Update food items
void updateFood(float deltaTime) {
  // Skip if snake hasn't started
  if (!snakeStarted || snakeSpine.getControlPoints().empty()) {
    return;
  }

  // Get snake head position

  // Update animation phases for all food items
  for (auto &food : foodItems) {
    food.animationPhase += deltaTime;
    if (food.animationPhase > 6.28f) { // 2π
      food.animationPhase -= 6.28f;
    }
  }

  // Check for collisions with snake head
  bool foodWasEaten = false;
  auto it = foodItems.begin();

  const glm::vec3 &snakeHead = deformedVertices[0];
  while (it != foodItems.end()) {
    glm::vec2 snakeXZ(snakeHead.x, snakeHead.z);
    glm::vec2 foodXZ(it->position.x, it->position.z);
    if (glm::distance(snakeXZ, foodXZ) < 1.0f) {
      it = foodItems.erase(it);
      foodWasEaten = true;
      pendingGrowth += 2;
      foodsEaten++;
      snakeSpeed += 0.05;
      printf("[Food] Eaten: %d\n", foodsEaten);
    } else {
      ++it;
    }
  }

  // CRITICAL: Always ensure we have exactly maxFoodItems on the map
  int missingItems = maxFoodItems - static_cast<int>(foodItems.size());

  if (missingItems > 0) {

    // Spawn exactly missingItems new food items
    for (int i = 0; i < missingItems; i++) {
      spawnFood();
    }
  }
}
// Initialize grid rendering resources
void initializeGrid() {
  // Create the grid data
  createGrid();

  // Generate buffers
  if (grid_vertex_buffer == 0) {
    glGenBuffers(1, &grid_vertex_buffer);
  }
  if (grid_color_buffer == 0) {
    glGenBuffers(1, &grid_color_buffer);
  }

  // Create VAO for desktop OpenGL
#ifndef USING_GLES
  if (grid_vao == 0) {
    glGenVertexArrays(1, &grid_vao);
    glBindVertexArray(grid_vao);
  }
#endif

  // Upload vertex data
  glBindBuffer(GL_ARRAY_BUFFER, grid_vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, gridVertices.size() * sizeof(glm::vec3),
               gridVertices.data(), GL_STATIC_DRAW);

  // Upload color data
  glBindBuffer(GL_ARRAY_BUFFER, grid_color_buffer);
  glBufferData(GL_ARRAY_BUFFER, gridColors.size() * sizeof(glm::vec3),
               gridColors.data(), GL_STATIC_DRAW);

#ifndef USING_GLES
  glBindVertexArray(0);
#endif
}

// Create vertex indices for a cube if no model is loaded
void createDefaultCube() {
  vertices = {// Front face
              {-0.5f, -0.5f, 0.5f},
              {0.5f, -0.5f, 0.5f},
              {0.5f, 0.5f, 0.5f},
              {-0.5f, 0.5f, 0.5f},
              // Back face
              {-0.5f, -0.5f, -0.5f},
              {0.5f, -0.5f, -0.5f},
              {0.5f, 0.5f, -0.5f},
              {-0.5f, 0.5f, -0.5f}};

  indices = {// Front
             0, 1, 2, 2, 3, 0,
             // Right
             1, 5, 6, 6, 2, 1,
             // Back
             5, 4, 7, 7, 6, 5,
             // Left
             4, 0, 3, 3, 7, 4,
             // Top
             3, 2, 6, 6, 7, 3,
             // Bottom
             4, 5, 1, 1, 0, 4};

  // Create basic UVs
  uvs.resize(vertices.size());
  for (size_t i = 0; i < vertices.size(); i++) {
    uvs[i] = glm::vec2((vertices[i].x + 0.5f), (vertices[i].y + 0.5f));
  }

  // Create basic normals
  normals.resize(vertices.size());
  for (size_t i = 0; i < vertices.size(); i++) {
    normals[i] = glm::normalize(vertices[i]);
  }
}

// Update GPU buffers with model data
void updateBuffers() {
  // Update both vertex buffers
  for (int i = 0; i < 2; i++) {
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[i]);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3),
                 vertices.data(), GL_DYNAMIC_DRAW);
  }
  // Initialize vertex change tracking
  lastDeformedVertices.resize(vertices.size());
  vertexChanged.resize(vertices.size());
  changedIndices.reserve(vertices.size() / 4); // Reserve reasonable capacity

  // Copy initial vertices
  for (size_t i = 0; i < vertices.size(); i++) {
    lastDeformedVertices[i] = vertices[i];
  }

  firstDeformation = true;

  // Update normal buffer
  glBindBuffer(GL_ARRAY_BUFFER, normal_buffer);
  glBufferData(GL_ARRAY_BUFFER, normals.size() * sizeof(glm::vec3),
               normals.data(), GL_STATIC_DRAW);

  // Update UV buffer
  glBindBuffer(GL_ARRAY_BUFFER, uv_buffer);
  glBufferData(GL_ARRAY_BUFFER, uvs.size() * sizeof(glm::vec2), uvs.data(),
               GL_STATIC_DRAW);

  // Update index buffer
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);

  // Reset current buffer to 0
  current_vertex_buffer = 0;

  modelLoaded = true;
}

void initializeSnakeSpine() {
  // Clear any existing control points
  snakeSpine.clear();

  // === Calculate model bounds ===
  glm::vec3 min(FLT_MAX), max(-FLT_MAX);
  for (const auto &v : vertices) {
    min = glm::min(min, v);
    max = glm::max(max, v);
  }

  // === Calculate snake body radius ===
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
  snakeBodyRadius = bodyThickness * 0.5f;
  printf("[initializeSnakeSpine] Snake body radius calculated: %.3f\n",
         snakeBodyRadius);

  // === Create initial control points ===
  const int numPoints = 20;
  float modelLength = max.x - min.x; // Assuming X is the primary axis
  float spacing = modelLength / (numPoints - 1);

  for (int i = 0; i < numPoints; i++) {
    float t = static_cast<float>(i) / (numPoints - 1);
    float x = max.x - t * modelLength;
    float y = (min.y + max.y) / 2.0f;
    float z = (min.z + max.z) / 2.0f;
    snakeSpine.addControlPoint(glm::vec3(x, y, z));
  }

  // === Reset state ===
  deformedVertices.resize(vertices.size());
  snakePosition = glm::vec3(0.0f);
  snakeDirection = glm::vec3(1.0f, 0.0f, 0.0f);
  nextDirection = snakeDirection;
  distanceSinceLastSample = 0.0f;
  snakeStarted = false;
  spineControlPointsChanged = true;
  cachedSpineFrames.clear();
  previousControlPoints = snakeSpine.getControlPoints();

  printf("Snake spine initialized with %zu control points\n",
         snakeSpine.getControlPoints().size());
  printf("Spacing between control points: %.2f\n", spacing);
}

// Enhanced function to calculate vertex arc lengths based on model geometry
void calculateVertexArcLengths() {
  vertexArcLengths.clear();
  vertexArcLengths.reserve(vertices.size());

  // Find model bounds
  glm::vec3 min(FLT_MAX), max(-FLT_MAX);
  for (const auto &v : vertices) {
    min = glm::min(min, v);
    max = glm::max(max, v);
  }

  // Model dimensions
  glm::vec3 dimensions = max - min;
  printf("Model dimensions: X: %f, Y: %f, Z: %f\n", dimensions.x, dimensions.y,
         dimensions.z);

  // For a snake model, we need to determine which axis represents its length
  // (usually the longest dimension)
  int primaryAxis = 0; // 0=X, 1=Y, 2=Z
  float maxDim = dimensions.x;

  if (dimensions.y > maxDim) {
    primaryAxis = 1;
    maxDim = dimensions.y;
  }

  if (dimensions.z > maxDim) {
    primaryAxis = 2;
    maxDim = dimensions.z;
  }

  printf("Primary axis for snake length: %d (0=X, 1=Y, 2=Z)\n", primaryAxis);

  // For each vertex, calculate its normalized position along the primary axis
  for (const auto &vertex : vertices) {
    float primaryCoord = 0.0f;
    float primaryMin = 0.0f;
    float primaryMax = 0.0f;

    // Get the appropriate coordinate based on primary axis
    switch (primaryAxis) {
    case 0: // X-axis
      primaryCoord = vertex.x;
      primaryMin = min.x;
      primaryMax = max.x;
      break;
    case 1: // Y-axis
      primaryCoord = vertex.y;
      primaryMin = min.y;
      primaryMax = max.y;
      break;
    case 2: // Z-axis
      primaryCoord = vertex.z;
      primaryMin = min.z;
      primaryMax = max.z;
      break;
    }

    // Normalize position along the primary axis (0.0 to 1.0)
    float normalizedPos =
        (primaryCoord - primaryMin) / (primaryMax - primaryMin);

    // We need to determine which end is the head and which is the tail
    // For most models, the head should be at the HIGHER value of the primary
    // axis For example, if Z is the primary axis, higher Z values would
    // typically be the head

    // Calculate arc length - invert the normalized position so that:
    // - Head vertices (highest coordinate) map to LOW arc lengths (front of
    // spine)
    // - Tail vertices (lowest coordinate) map to HIGH arc lengths (back of
    // spine)
    float arcLength = (1.0f - normalizedPos) * segmentCount * segmentSpacing;

    vertexArcLengths.push_back(arcLength);
  }

  // Find the min and max arc lengths assigned to verify our mapping
  float minArc = FLT_MAX;
  float maxArc = -FLT_MAX;
  for (float arc : vertexArcLengths) {
    minArc = std::min(minArc, arc);
    maxArc = std::max(maxArc, arc);
  }

  printf("Arc length range: %f to %f\n", minArc, maxArc);
  printf(
      "Vertices mapped from head (arc length %.2f) to tail (arc length %.2f)\n",
      minArc, maxArc);

  // If we detect a potential issue with mapping direction, warn the user
  if (maxArc - minArc < segmentCount * segmentSpacing * 0.5f) {
    printf("WARNING: Arc length range seems too small. The snake model might "
           "not deform properly.\n");
    printf("Consider adjusting segmentCount (%d) or segmentSpacing (%.2f)\n",
           segmentCount, segmentSpacing);
  }
}

// Function to load a model
bool loadModel(const char *filename) {
  printf("Loading model: %s\n", filename);

  // Save the current file
  currentModelFile = filename;

  // Clear existing data
  vertices.clear();
  uvs.clear();
  normals.clear();
  indices.clear();

  // Try to load the model
  bool result = loadModelBase(filename, vertices, uvs, normals, indices);

  if (!result || vertices.empty()) {
    printf("Failed to load model or model was empty. Using default cube.\n");
    createDefaultCube();
    return false;
  }

  printf("Successfully loaded model with %zu vertices, %zu indices\n",
         vertices.size(), indices.size());

  // Update GPU buffers
  updateBuffers();

  firstDeformation = true;
  lastDeformedVertices.resize(vertices.size());
  vertexChanged.resize(vertices.size());
  changedIndices.clear();

#ifdef __EMSCRIPTEN__
  // Resize vertex data buffer to match new model
  vertexDataBuffer.resize(vertices.size() * 3);

  // Initialize with vertex data
  for (size_t i = 0; i < vertices.size(); i++) {
    vertexDataBuffer[i * 3] = vertices[i].x;
    vertexDataBuffer[i * 3 + 1] = vertices[i].y;
    vertexDataBuffer[i * 3 + 2] = vertices[i].z;
  }

  // Update the pointer and size in JavaScript
  EM_ASM(
      {
        Module.vertexBufferPointer = $0;
        Module.vertexBufferSize = $1;
      },
      vertexDataBuffer.data(), vertexDataBuffer.size());
#endif

  // Calculate vertex arc lengths based on model geometry
  calculateVertexArcLengths();

  // Initialize the snake spine with proper orientation
  initializeSnakeSpine();

  bindVerticesToSpine(snakeSpine, vertices);

  spineControlPointsChanged = true;
  cachedSpineFrames.clear();

  // Resize deformed vertices buffer
  deformedVertices.resize(vertices.size());

  return true;
}

// Initialize OpenGL
bool init_opengl() {
  if (!glfwInit()) {
    printf("Failed to initialize GLFW\n");
    return false;
  }

#ifdef __EMSCRIPTEN__
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#endif

  g_window = glfwCreateWindow(g_window_width, g_window_height,
                              "3D Model Renderer", nullptr, nullptr);
  if (!g_window) {
    printf("Failed to create GLFW window\n");
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(g_window);

  int framebuffer_width, framebuffer_height;
  glfwGetFramebufferSize(g_window, &framebuffer_width, &framebuffer_height);
  glViewport(0, 0, framebuffer_width, framebuffer_height);
  glfwSetFramebufferSizeCallback(g_window,
                                 [](GLFWwindow *, int width, int height) {
                                   glViewport(0, 0, width, height);
                                 });

  // Create shader program
  program_id = create_program();
  if (!program_id) {
    printf("Failed to create shader program\n");
    return false;
  }

  // Get uniform and attribute locations
  mvp_location = glGetUniformLocation(program_id, "MVP");
  model_matrix_location = glGetUniformLocation(program_id, "modelMatrix");
  vertex_position_location = glGetAttribLocation(program_id, "vertexPosition");
  vertex_normal_location = glGetAttribLocation(program_id, "vertexNormal");
  vertex_uv_location = glGetAttribLocation(program_id, "vertexUV");

// Create and bind VAO (not available in OpenGL ES 2.0, but we can create it for
// desktop OpenGL)
#ifndef USING_GLES
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
#endif

  // Create vertex position buffer
  glGenBuffers(2, vertex_buffers);
  for (int i = 0; i < 2; i++) {
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[i]);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3),
                 vertices.data(), GL_DYNAMIC_DRAW); // Note: Using DYNAMIC_DRAW
  }

  // Create normal buffer
  glGenBuffers(1, &normal_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, normal_buffer);
  glBufferData(GL_ARRAY_BUFFER, normals.size() * sizeof(glm::vec3),
               normals.data(), GL_STATIC_DRAW);

  // Create UV buffer
  glGenBuffers(1, &uv_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, uv_buffer);
  glBufferData(GL_ARRAY_BUFFER, uvs.size() * sizeof(glm::vec2), uvs.data(),
               GL_STATIC_DRAW);

  // Create index buffer
  glGenBuffers(1, &index_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);

#ifdef __EMSCRIPTEN__
  // This hint improves performance when repeatedly uploading to buffers
  EM_ASM({
    // Force WebGL to preserve buffer bindings
    Module.ctx.preventBufferBind = true;
    console.log("Optimized for dynamic buffer updates");
  });
#endif

  initializeGrid();
  initializeFood();

  // Set up camera
  glm::vec3 cameraPosition = glm::vec3(0.0f, 60.0f, 0.0f);

  glm::vec3 targetPosition = glm::vec3(0.0f, 0.0f, 0.0f);

  glm::vec3 upVector = glm::vec3(0.0f, 0.0f, -1.0f);

  view_matrix = glm::lookAt(cameraPosition, targetPosition, upVector);

  projection_matrix = glm::perspective(
      glm::radians(45.0f), (float)g_window_width / (float)g_window_height, 0.1f,
      100.0f);

  // Set up model matrix
  model_matrix = glm::mat4(1.0f);

  // Set up OpenGL state
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glClearColor(0.0f, 0.02f, 0.05f,
               1.0f); // Same green background as original code

#ifndef USING_GLES
  // Use wireframe mode for better visualization (not supported in OpenGL ES)
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif

// Set swap interval ONLY on desktop platforms, not on Emscripten
#ifndef __EMSCRIPTEN__
  glfwSwapInterval(1);
#endif

// Try to load a model at startup
#ifdef __EMSCRIPTEN__
  loadModel("/assets/models/snakeH.gltf");
#else
  loadModel("assets/models/snakeH.obj");
#endif

  return true;
}

glm::vec3 closestPointOnSegment(const glm::vec3 &point, const glm::vec3 &a,
                                const glm::vec3 &b) {
  glm::vec3 ab = b - a;
  float t = glm::dot(point - a, ab) / glm::dot(ab, ab);
  t = glm::clamp(t, 0.0f, 1.0f);
  return a + t * ab;
}

void updateSnake(float deltaTime) {
  if (!snakeStarted || snakeSpine.getControlPoints().empty() || gameOver) {
    return;
  }

  // === Copy control points ===
  std::vector<glm::vec3> controlPoints = snakeSpine.getControlPoints();

  // === Apply pending growth FIRST ===
  while (pendingGrowth > 0) {
    if (controlPoints.size() < 2)
      break; // Safety

    glm::vec3 tail = controlPoints.back();
    glm::vec3 secondLast = controlPoints[controlPoints.size() - 2];
    glm::vec3 tailDir = glm::normalize(tail - secondLast);

    glm::vec3 newPoint = tail + tailDir * (-gridScale);
    controlPoints.push_back(newPoint);

    pendingGrowth--;
    printf("[Snake Length] Control Points: %zu | Spine Frames: %zu\n",
           snakeSpine.getControlPoints().size(), cachedSpineFrames.size());
  }

  // === Move Snake Head ===
  controlPoints[0] += nextDirection * snakeSpeed * deltaTime;

  // === Update Snake Body ===
  for (size_t i = 1; i < controlPoints.size(); i++) {
    glm::vec3 direction = controlPoints[i - 1] - controlPoints[i];
    float distance = glm::length(direction);

    if (distance > gridScale) {
      controlPoints[i] += glm::normalize(direction) * (distance - gridScale);
    }
  }

  // === Update Spine ===
  snakeSpine.clear();
  for (const auto &point : controlPoints) {
    snakeSpine.addControlPoint(point);
  }

  spineControlPointsChanged = true;

  // Boundary collision

  controlPoints = snakeSpine.getControlPoints();
  glm::vec3 head = controlPoints[0];

  float aspectRatio = (float)g_window_width / (float)g_window_height;
  float halfHeight = cameraHeight * tan(glm::radians(fovY * 0.5f));
  float halfWidth = halfHeight * aspectRatio;

  if (abs(head.x) > halfWidth || abs(head.z) > halfHeight) {
    gameOver = true;
    nextDirection = glm::vec3(0.0f);
    printf("Boundary collision! Game Over.\n");
    return;
  }

  // === Self-collision detection ===
  const float collisionRadius = 0.4f; // Precomputed once at start

  controlPoints = snakeSpine.getControlPoints();
  for (size_t i = 4; i < snakeSpine.getControlPoints().size(); ++i) {
    const glm::vec3 &bodyPoint = controlPoints[i];

    float dist = glm::distance(snakeSpine.getControlPoints()[0], bodyPoint);

    if (dist < 2.0f * collisionRadius) { // 2x body radius
      gameOver = true;
      nextDirection = glm::vec3(0.0f);
      printf("Self-collision detected!\n");
      break;
    }
  }
}

void restartGame() {
  printf("Restarting game...\n");

  // Clear snake
  snakeSpine.clear();
  spineControlPointsChanged = true;
  pendingGrowth = 0;

  // Reinitialize snake
  initializeSnakeSpine();

  // Clear food
  foodItems.clear();
  for (int i = 0; i < maxFoodItems; ++i) {
    spawnFood();
  }

  // Reset direction
  nextDirection = glm::vec3(1, 0, 0); // or whatever you want for default

  // Reset game state
  gameOver = false;
  snakeStarted = false; // force player to press key to start
}

// Update scene
void update() {
  float currentTime = glfwGetTime();
  deltaTime = currentTime - lastTime;
  lastTime = currentTime;

  // Limit delta time to prevent large jumps on lag
  deltaTime = glm::min(deltaTime, 0.1f);

  // Handle input (WASD) with correct mapping to world space
  if (!gameOver) {
    if (glfwGetKey(g_window, GLFW_KEY_W) == GLFW_PRESS) {
      nextDirection = glm::vec3(0.0f, 0.0f, -1.0f); // Forward in screen (-Z)
      snakeStarted = true;
    } else if (glfwGetKey(g_window, GLFW_KEY_S) == GLFW_PRESS) {
      nextDirection = glm::vec3(0.0f, 0.0f, 1.0f); // Backward in screen (+Z)
      snakeStarted = true;
    } else if (glfwGetKey(g_window, GLFW_KEY_A) == GLFW_PRESS) {
      nextDirection = glm::vec3(-1.0f, 0.0f, 0.0f); // Left (-X)
      snakeStarted = true;
    } else if (glfwGetKey(g_window, GLFW_KEY_D) == GLFW_PRESS) {
      nextDirection = glm::vec3(1.0f, 0.0f, 0.0f); // Right (+X)
      snakeStarted = true;
    }
  }
  // Reset with R key
  if (glfwGetKey(g_window, GLFW_KEY_R) == GLFW_PRESS) {
    restartGame();
    return;
  }

  // Update snake movement and control points
  updateSnake(deltaTime);

  // Deform the mesh using bindings
  if (vertices.size() == deformedVertices.size() &&
      vertexBindings.size() == vertices.size()) {

    // Deform the mesh using spine
    deformMeshToSpline(vertices, deformedVertices, snakeSpine);

    // Clear previous changed status
    vertexChanged.assign(vertices.size(), false);
    changedIndices.clear();

    // First frame after loading - force full update
    if (firstDeformation) {
      firstDeformation = false;

      // Copy to lastDeformedVertices for next frame comparison
      lastDeformedVertices = deformedVertices;

      // Ping-pong to the next buffer
      int next_buffer = (current_vertex_buffer + 1) % 2;

      // Update the entire buffer
      glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[next_buffer]);
      glBufferData(GL_ARRAY_BUFFER, deformedVertices.size() * sizeof(glm::vec3),
                   deformedVertices.data(), GL_DYNAMIC_DRAW);

      // Switch to the updated buffer
      current_vertex_buffer = next_buffer;
      return;
    }

    // Check which vertices have changed significantly
    for (size_t i = 0; i < vertices.size(); i++) {
      // Calculate squared distance - much faster than actual distance
      float dx = deformedVertices[i].x - lastDeformedVertices[i].x;
      float dy = deformedVertices[i].y - lastDeformedVertices[i].y;
      float dz = deformedVertices[i].z - lastDeformedVertices[i].z;
      float distSquared = dx * dx + dy * dy + dz * dz;

      // If vertex moved enough, mark it as changed
      if (distSquared > vertexChangeThreshold) {
        vertexChanged[i] = true;
        changedIndices.push_back(i);
        // Update lastDeformedVertices for next frame
        lastDeformedVertices[i] = deformedVertices[i];
      }
    }

    // Skip buffer update if nothing changed
    if (changedIndices.empty()) {
      return;
    }

    // Ping-pong to the next buffer
    int next_buffer = (current_vertex_buffer + 1) % 2;
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[next_buffer]);

    // Different strategies for WebGL vs desktop
#ifdef __EMSCRIPTEN__
    // For WebGL: If many vertices changed, update the whole buffer
    if (changedIndices.size() > vertices.size() * 0.1) { // 10% threshold
      // Full buffer update
      glBufferData(GL_ARRAY_BUFFER, deformedVertices.size() * sizeof(glm::vec3),
                   deformedVertices.data(), GL_DYNAMIC_DRAW);
    } else {
      // WebGL: Use batched updates for better performance
      const int BATCH_SIZE = 100; // vertices per batch

      // Sort indices to ensure contiguous memory where possible
      std::sort(changedIndices.begin(), changedIndices.end());

      // Start with initial values
      unsigned int batchStart = changedIndices[0];
      unsigned int batchEnd = batchStart;

      for (size_t i = 1; i < changedIndices.size(); i++) {
        unsigned int idx = changedIndices[i];

        // If this index is contiguous with the current batch, extend the batch
        if (idx == batchEnd + 1) {
          batchEnd = idx;
        } else {
          // Non-contiguous index, update the current batch first
          unsigned int batchSize = batchEnd - batchStart + 1;
          glBufferSubData(GL_ARRAY_BUFFER, batchStart * sizeof(glm::vec3),
                          batchSize * sizeof(glm::vec3),
                          &deformedVertices[batchStart]);

          // Start a new batch
          batchStart = idx;
          batchEnd = idx;
        }

        // Update the final batch
        if (i == changedIndices.size() - 1) {
          unsigned int batchSize = batchEnd - batchStart + 1;
          glBufferSubData(GL_ARRAY_BUFFER, batchStart * sizeof(glm::vec3),
                          batchSize * sizeof(glm::vec3),
                          &deformedVertices[batchStart]);
        }
      }
    }
#else
    // Desktop OpenGL: If few vertices changed (less than 30%), use individual
    // updates
    if (changedIndices.size() < vertices.size() * 0.3) {
      for (unsigned int idx : changedIndices) {
        // Update just this vertex
        glBufferSubData(GL_ARRAY_BUFFER, idx * sizeof(glm::vec3),
                        sizeof(glm::vec3), &deformedVertices[idx]);
      }
    } else {
      // Many vertices changed, just update the whole buffer
      glBufferSubData(GL_ARRAY_BUFFER, 0,
                      deformedVertices.size() * sizeof(glm::vec3),
                      deformedVertices.data());
    }
#endif

    // Switch to the updated buffer
    current_vertex_buffer = next_buffer;
  }

  updateFood(deltaTime);

  // Static model matrix
  model_matrix = glm::mat4(1.0f);
  mvp_matrix = projection_matrix * view_matrix * model_matrix;
}

// Render food items
void renderFood() {
  if (foodItems.empty() || food_program_id == 0)
    return;

  glUseProgram(food_program_id);

  // Set common uniforms
  glm::mat4 mvp = projection_matrix * view_matrix * model_matrix;
  glUniformMatrix4fv(food_mvp_location, 1, GL_FALSE, glm::value_ptr(mvp));
  glUniform1f(food_time_location, glfwGetTime());

  // Create or update buffer with all positions at once
  glBindBuffer(GL_ARRAY_BUFFER, food_vertex_buffer);

  // Extract all positions into a contiguous array
  std::vector<glm::vec3> allPositions;
  std::vector<float> allGlowFactors;
  std::vector<glm::vec3> allColors;

  for (const auto &food : foodItems) {
    allPositions.push_back(food.position);
    allGlowFactors.push_back(food.glowIntensity);
    allColors.push_back(food.color);
  }

  // Update buffers with all data at once
  glBindBuffer(GL_ARRAY_BUFFER, food_vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, allPositions.size() * sizeof(glm::vec3),
               allPositions.data(), GL_DYNAMIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, food_glow_buffer);
  glBufferData(GL_ARRAY_BUFFER, allGlowFactors.size() * sizeof(float),
               allGlowFactors.data(), GL_DYNAMIC_DRAW);

  // Create a buffer for colors if it doesn't exist
  static GLuint food_color_buffer = 0;
  if (food_color_buffer == 0) {
    glGenBuffers(1, &food_color_buffer);
  }

  glBindBuffer(GL_ARRAY_BUFFER, food_color_buffer);
  glBufferData(GL_ARRAY_BUFFER, allColors.size() * sizeof(glm::vec3),
               allColors.data(), GL_DYNAMIC_DRAW);

  // Enable needed attributes
  model_matrix = glm::mat4(1.0f);
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, food_vertex_buffer);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);

  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, food_glow_buffer);
  glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, 0);

  // Setup uniforms for rendering
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);

  // Draw all food items in a single draw call
  glDrawArrays(GL_POINTS, 0, foodItems.size());

  // Cleanup
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_BLEND);
  glUseProgram(0);
}

// Render the grid
void renderGrid() {
  // Skip if no grid has been created
  if (gridVertices.empty() || grid_vertex_buffer == 0) {
    return;
  }

  // Use shader program
  glUseProgram(program_id);

  // Set MVP matrix
  glm::mat4 grid_mvp = projection_matrix * view_matrix * model_matrix;
  glUniformMatrix4fv(mvp_location, 1, GL_FALSE, glm::value_ptr(grid_mvp));
  glUniformMatrix4fv(glGetUniformLocation(program_id, "modelMatrix"), 1,
                     GL_FALSE, glm::value_ptr(model_matrix));

  // Enable blending for transparency
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Enable line smoothing if available (not in WebGL/ES)
#ifndef USING_GLES
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
#endif

  // Position attribute
  GLuint pos_loc = glGetAttribLocation(program_id, "vertexPosition");
  GLuint color_loc = glGetAttribLocation(program_id, "vertexColor");

  glEnableVertexAttribArray(pos_loc);
  glBindBuffer(GL_ARRAY_BUFFER, grid_vertex_buffer);
  glVertexAttribPointer(pos_loc, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);

  glEnableVertexAttribArray(color_loc);
  glBindBuffer(GL_ARRAY_BUFFER, grid_color_buffer);
  glVertexAttribPointer(color_loc, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);

  // We need to modify the grid colors to have transparency
  // Since we can't change createGrid, we'll use a uniform to apply transparency
  GLuint opacity_loc = glGetUniformLocation(program_id, "opacity");

  // Draw regular grid lines with low opacity (more blurry/subtle)
  glUniform1f(opacity_loc, 0.15f); // 15% opacity for regular grid lines

  // Determine how many vertices are regular grid lines (total - border lines)
  int regularGridVertices = gridVertices.size() - 8;
  glDrawArrays(GL_LINES, 0, regularGridVertices);

  // Draw border with slightly higher opacity
  glUniform1f(opacity_loc, 0.4f); // 40% opacity for border
  glDrawArrays(GL_LINES, regularGridVertices, 8);

  // Restore OpenGL state
  glDisableVertexAttribArray(pos_loc);
  glDisableVertexAttribArray(color_loc);

#ifndef USING_GLES
  glDisable(GL_LINE_SMOOTH);
#endif

  glDisable(GL_BLEND);
}

void updateViewportIfNeeded() {
  int width, height;
  glfwGetFramebufferSize(g_window, &width, &height);
  glViewport(0, 0, width, height);
}

// Render scene
void render() {
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  updateViewportIfNeeded();
  renderGrid();
  renderFood();

  if (!modelLoaded) {
    return; // Nothing to render
  }

  // Use shader program
  glUseProgram(program_id);

  // Only update MVP if changed
  if (!uniformCache.initialized ||
      memcmp(&uniformCache.lastMVP, &mvp_matrix, sizeof(glm::mat4)) != 0) {
    glUniformMatrix4fv(mvp_location, 1, GL_FALSE, glm::value_ptr(mvp_matrix));
    uniformCache.lastMVP = mvp_matrix;
  }

  // Only update model matrix if changed
  GLuint model_matrix_location =
      glGetUniformLocation(program_id, "modelMatrix");
  if (!uniformCache.initialized ||
      memcmp(&uniformCache.lastModelMatrix, &model_matrix, sizeof(glm::mat4)) !=
          0) {
    glUniformMatrix4fv(model_matrix_location, 1, GL_FALSE,
                       glm::value_ptr(model_matrix));
    uniformCache.lastModelMatrix = model_matrix;
  }

  // Mark as initialized
  uniformCache.initialized = true;

// For OpenGL ES / WebGL
#ifdef USING_GLES

  // **** CRITICAL CHANGE: Use the current buffer ****
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[current_vertex_buffer]);
  glVertexAttribPointer(vertex_position_location, 3, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(vertex_position_location);

  // Normal attribute
  glBindBuffer(GL_ARRAY_BUFFER, normal_buffer);
  glVertexAttribPointer(vertex_normal_location, 3, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(vertex_normal_location);

  // UV attribute
  glBindBuffer(GL_ARRAY_BUFFER, uv_buffer);
  glVertexAttribPointer(vertex_uv_location, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(vertex_uv_location);

  // Bind indices
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
#else
  // Desktop OpenGL with VAO
  glBindVertexArray(vao);

  // Update VAO to point to current buffer
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[current_vertex_buffer]);
  glVertexAttribPointer(vertex_position_location, 3, GL_FLOAT, GL_FALSE, 0, 0);
#endif

  // Draw elements
  glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);

// Clean up
#ifdef USING_GLES

  glDisableVertexAttribArray(vertex_position_location);
  glDisableVertexAttribArray(vertex_normal_location);
  glDisableVertexAttribArray(vertex_uv_location);
#endif
}

// Main loop function for Emscripten
void main_loop() {
  update();
  render();
  glfwSwapBuffers(g_window);
  glfwPollEvents();
}

// Exposed function to load models from JavaScript
#ifdef __EMSCRIPTEN__
extern "C" {
EMSCRIPTEN_KEEPALIVE
int loadModelFromJS(const char *filename) {
  bool result = ::loadModel(filename);
  return result ? 1 : 0;
}
}
#endif

// Clean up
void cleanup() {
#ifdef __EMSCRIPTEN__
  // Clear JS references
  EM_ASM({
    Module.vertexBufferPointer = null;
    Module.vertexBufferSize = 0;
  });

  // Vector will clean itself up automatically
  vertexDataBuffer.clear();
#endif
  glDeleteBuffers(2, vertex_buffers);
  if (normal_buffer) {
    glDeleteBuffers(1, &normal_buffer);
  }
  if (index_buffer) {
    glDeleteBuffers(1, &index_buffer);
  }
  if (uv_buffer) {
    glDeleteBuffers(1, &uv_buffer);
  }
  if (vao) {
    glDeleteVertexArrays(1, &vao);
  }
  if (program_id) {
    glDeleteProgram(program_id);
  }
  if (food_program_id) {
    glDeleteProgram(food_program_id);
  }
  if (food_vertex_buffer) {
    glDeleteBuffers(1, &food_vertex_buffer);
  }
  if (food_glow_buffer) {
    glDeleteBuffers(1, &food_glow_buffer);
  }

  glfwDestroyWindow(g_window);
  glfwTerminate();
}

int main() {

  if (!init_opengl()) {
    return -1;
  }

  lastTime = glfwGetTime();

#ifdef __EMSCRIPTEN__
  // Emscripten version - pass the main loop function
  emscripten_set_main_loop(main_loop, 0, 1);

  // NOW we can set the timing mode (after the main loop is created)
  emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
#else
  // Desktop version - run until window should close
  while (!glfwWindowShouldClose(g_window)) {
    main_loop();
  }

  cleanup();
#endif

  return 0;
}
