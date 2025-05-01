#pragma once

#include "Snake.hpp"
#include <vector>

struct FoodItem {
  glm::vec3 position;
  float size;
  glm::vec3 color;
  float glowIntensity;
  float animationPhase;
};

enum class GameState { MENU, CUSTOMIZATION, GAMEPLAY, GAME_OVER };

class GameManager {
private:
  // Game state
  GameState currentState;
  bool gameOver;

  // Snakes
  Snake player1;
  Snake player2;
  bool isMultiplayer;

  // Food items
  std::vector<FoodItem> foodItems;
  int maxFoodItems;
  float foodSpawnTimer;
  float foodSpawnInterval;

  // Rendering resources
  GLuint mainShaderProgram;
  GLuint foodShaderProgram;
  GLuint gridVertexBuffer;
  GLuint gridColorBuffer;
  std::vector<glm::vec3> gridVertices;
  std::vector<glm::vec3> gridColors;

  // Camera and matrices
  glm::mat4 viewMatrix;
  glm::mat4 projectionMatrix;
  float cameraHeight;

public:
  GameManager();
  ~GameManager();

  void initialize();
  void update(float deltaTime);
  void render();

  void handleInput();
  void switchState(GameState newState);

  void spawnFood();
  void checkCollisions();

  Snake &getPlayer1() { return player1; }
  Snake &getPlayer2() { return player2; }

  bool isGameOver() const { return gameOver; }
  void restartGame();

private:
  void initializeGrid();
  void initializeFood();
  void renderGrid();
  void renderFood();
  void updateFood(float deltaTime);
  void createShaders();
};
