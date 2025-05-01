#pragma once

#include <glm/glm.hpp>
#include <vector>

struct SplineFrame {
  glm::vec3 position;
  glm::vec3 tangent;
  glm::vec3 normal;
  glm::vec3 binormal;
};

struct VertexBinding {
  int frame_index;
  glm::vec3 local_coords;
};

class Spline {
public:
  void addControlPoint(const glm::vec3 &point);
  void clear();

  glm::vec3 evaluate(float t) const;
  SplineFrame evaluateFrame(float t) const;
  SplineFrame evaluateFrame(float distance,
                            const SplineFrame &previousFrame) const;
  float getLength() const;

  std::vector<glm::vec3> getControlPoints() const;

  glm::vec3 computeTangent(float t) const;

  SplineFrame computeFrame(float t) const;

private:
  std::vector<glm::vec3> controlPoints;

  glm::vec3 catmullRom(float t, int i) const;
};

// Global Catmull-Rom interpolation function
glm::vec3 catmullRomGlobal(const glm::vec3 &p0, const glm::vec3 &p1,
                           const glm::vec3 &p2, const glm::vec3 &p3, float t);

void bindVerticesToSpine(const Spline &spine,
                         const std::vector<glm::vec3> &vertices);
