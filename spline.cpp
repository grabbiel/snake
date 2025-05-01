#include "spline.hpp"
#include "deform.hpp"
#include "globals.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/norm.hpp"
#include <glm/gtx/compatibility.hpp>
#include <glm/gtx/normalize_dot.hpp>
#include <glm/gtx/orthonormalize.hpp>

void Spline::addControlPoint(const glm::vec3 &point) {
  controlPoints.push_back(point);
}

void Spline::clear() { controlPoints.clear(); }

std::vector<glm::vec3> Spline::getControlPoints() const {
  return controlPoints;
}

float Spline::getLength() const {
  return static_cast<float>(controlPoints.size());
}

glm::vec3 Spline::evaluate(float t) const {
  if (controlPoints.size() < 4)
    return glm::vec3(0.0f);

  // Clamp t to valid range
  t = glm::clamp(t, 0.0f, getLength() - 1.0f);

  int seg = static_cast<int>(t);
  float lt = t - static_cast<float>(seg);

  int p0, p1, p2, p3;

  p1 = glm::clamp(seg, 0, (int)controlPoints.size() - 1);
  p0 = glm::clamp(p1 - 1, 0, (int)controlPoints.size() - 1);
  p2 = glm::clamp(p1 + 1, 0, (int)controlPoints.size() - 1);
  p3 = glm::clamp(p2 + 1, 0, (int)controlPoints.size() - 1);

  return catmullRomGlobal(controlPoints[p0], controlPoints[p1],
                          controlPoints[p2], controlPoints[p3], lt);
}

// Catmull-Rom interpolation with uniform parameterization
glm::vec3 Spline::catmullRom(float t, int i) const {
  const glm::vec3 &p0 = controlPoints[i];
  const glm::vec3 &p1 = controlPoints[i + 1];
  const glm::vec3 &p2 = controlPoints[i + 2];
  const glm::vec3 &p3 = controlPoints[i + 3];

  float t2 = t * t;
  float t3 = t2 * t;

  return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                 (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                 (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

glm::vec3 Spline::computeTangent(float t) const {
  // Use central difference for more accurate tangent calculation
  const float h = 0.01f;

  // Handle empty spline case
  if (controlPoints.size() < 4)
    return glm::vec3(1.0f, 0.0f, 0.0f);

  // Calculate points slightly before and after t
  float t1 = glm::max(0.0f, t - h);
  float t2 = glm::min(getLength(), t + h);

  // If t1 and t2 are the same, we're at an endpoint
  if (fabs(t2 - t1) < 1e-5f) {
    // If at the start, use forward difference
    if (t < h) {
      t1 = 0.0f;
      t2 = h;
    }
    // If at the end, use backward difference
    else {
      t1 = getLength() - h;
      t2 = getLength();
    }
  }

  glm::vec3 p1 = evaluate(t1);
  glm::vec3 p2 = evaluate(t2);

  // Calculate tangent as the difference
  glm::vec3 tangent = p2 - p1;

  // If the tangent is too small, try with a larger step
  if (glm::length2(tangent) < 1e-5f) {
    t1 = glm::max(0.0f, t - 0.1f);
    t2 = glm::min(getLength(), t + 0.1f);
    p1 = evaluate(t1);
    p2 = evaluate(t2);
    tangent = p2 - p1;

    // If still too small, use a default direction
    if (glm::length2(tangent) < 1e-5f) {
      // Try to infer direction from control points
      if (!controlPoints.empty() && controlPoints.size() >= 2) {
        int mid = controlPoints.size() / 2;
        tangent = controlPoints[0] - controlPoints[mid];
        if (glm::length2(tangent) < 1e-5f) {
          return glm::vec3(1.0f, 0.0f, 0.0f);
        }
      } else {
        return glm::vec3(1.0f, 0.0f, 0.0f);
      }
    }
  }

  return glm::normalize(tangent);
}

SplineFrame Spline::computeFrame(float t) const {
  SplineFrame frame;

  // Handle empty spline case
  if (controlPoints.size() < 4) {
    frame.position = glm::vec3(0.0f);
    frame.tangent = glm::vec3(1.0f, 0.0f, 0.0f);  // Forward direction
    frame.normal = glm::vec3(0.0f, 1.0f, 0.0f);   // Up direction
    frame.binormal = glm::vec3(0.0f, 0.0f, 1.0f); // Right direction
    return frame;
  }

  // Get position by evaluating spline
  frame.position = evaluate(t);

  // Calculate tangent (direction of movement)
  frame.tangent = computeTangent(t);

  // Choose a consistent up vector for initial frame calculation
  glm::vec3 worldUp(0.0f, 1.0f, 0.0f); // World up is Y-axis

  // If tangent is too close to world up, choose a different reference vector
  if (glm::abs(glm::dot(worldUp, frame.tangent)) > 0.99f) {
    worldUp = glm::vec3(1.0f, 0.0f, 0.0f); // Use X-axis instead
  }

  // Calculate binormal (perpendicular to tangent and world up)
  // This gives us the "right" direction of the frame
  frame.binormal = glm::normalize(glm::cross(frame.tangent, worldUp));

  // Calculate normal (perpendicular to tangent and binormal)
  // This gives us the "up" direction of the frame
  frame.normal = glm::normalize(glm::cross(frame.binormal, frame.tangent));

  return frame;
}

SplineFrame Spline::evaluateFrame(float t,
                                  const SplineFrame &previousFrame) const {
  // If we don't have enough control points, return previous frame
  if (controlPoints.size() < 4)
    return previousFrame;

  // Get current position and calculate tangent
  glm::vec3 position = evaluate(t);
  glm::vec3 tangent = computeTangent(t);

  // Create rotation matrix from previous frame's basis vectors
  // Column 0 = normal, Column 1 = binormal, Column 2 = tangent
  glm::mat3 prevBasis(previousFrame.normal, previousFrame.binormal,
                      previousFrame.tangent);

  // Use parallel transport to rotate the frame
  // This minimizes twisting along the spline
  glm::mat3 currentBasis =
      parallelTransport(prevBasis, previousFrame.tangent, tangent);

  // Create new frame
  SplineFrame frame;
  frame.position = position;
  frame.tangent = tangent;

  // Extract normal and binormal from rotated basis
  frame.normal = currentBasis[0];   // First column
  frame.binormal = currentBasis[1]; // Second column

  // Ensure orthogonality
  frame.binormal = glm::normalize(glm::cross(frame.tangent, frame.normal));
  frame.normal = glm::normalize(glm::cross(frame.binormal, frame.tangent));

  return frame;
}

SplineFrame Spline::evaluateFrame(float distance) const {
  if (controlPoints.empty()) {
    // Return default frame if no control points
    SplineFrame frame;
    frame.position = glm::vec3(0.0f);
    frame.tangent = glm::vec3(0.0f, 0.0f, 1.0f);
    frame.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    frame.binormal = glm::vec3(1.0f, 0.0f, 0.0f);
    return frame;
  }

  if (controlPoints.size() == 1) {
    // If only one control point, use it for position
    SplineFrame frame;
    frame.position = controlPoints[0];
    frame.tangent = glm::vec3(0.0f, 0.0f, 1.0f);
    frame.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    frame.binormal = glm::vec3(1.0f, 0.0f, 0.0f);
    return frame;
  }

  // For multiple control points, compute initial frame
  float segmentLength = gridSize;
  int segmentIndex = static_cast<int>(distance / segmentLength);
  float localT = (distance - (segmentIndex * segmentLength)) / segmentLength;

  // Clamp indices for safety
  segmentIndex = glm::clamp(segmentIndex, 0, (int)controlPoints.size() - 2);

  // Ensure we have enough points for Catmull-Rom
  if (controlPoints.size() >= 4) {
    // Full Catmull-Rom calculation
    int p0_idx = glm::max(0, segmentIndex - 1);
    int p1_idx = segmentIndex;
    int p2_idx = glm::min((int)controlPoints.size() - 1, segmentIndex + 1);
    int p3_idx = glm::min((int)controlPoints.size() - 1, segmentIndex + 2);

    glm::vec3 p0 = controlPoints[p0_idx];
    glm::vec3 p1 = controlPoints[p1_idx];
    glm::vec3 p2 = controlPoints[p2_idx];
    glm::vec3 p3 = controlPoints[p3_idx];

    SplineFrame frame;
    frame.position = catmullRomGlobal(p0, p1, p2, p3, localT);

    // Calculate tangent
    glm::vec3 nextPos = catmullRomGlobal(p0, p1, p2, p3, localT + 0.01f);
    frame.tangent = glm::normalize(nextPos - frame.position);

    // Calculate normal and binormal
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    if (glm::abs(glm::dot(worldUp, frame.tangent)) > 0.99f) {
      worldUp = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    frame.normal = glm::normalize(
        glm::cross(glm::cross(frame.tangent, worldUp), frame.tangent));
    frame.binormal = glm::normalize(glm::cross(frame.tangent, frame.normal));

    return frame;
  } else {
    // Linear interpolation for 2-3 control points
    int p1_idx = glm::min(segmentIndex, (int)controlPoints.size() - 2);
    int p2_idx = p1_idx + 1;

    SplineFrame frame;
    frame.position =
        glm::mix(controlPoints[p1_idx], controlPoints[p2_idx], localT);
    frame.tangent =
        glm::normalize(controlPoints[p2_idx] - controlPoints[p1_idx]);

    // Set normal and binormal
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    if (glm::abs(glm::dot(worldUp, frame.tangent)) > 0.99f) {
      worldUp = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    frame.normal = glm::normalize(
        glm::cross(glm::cross(frame.tangent, worldUp), frame.tangent));
    frame.binormal = glm::normalize(glm::cross(frame.tangent, frame.normal));

    return frame;
  }
}

glm::vec3 catmullRomGlobal(const glm::vec3 &p0, const glm::vec3 &p1,
                           const glm::vec3 &p2, const glm::vec3 &p3, float t) {
  // Precompute basis functions once
  const float t2 = t * t;
  const float t3 = t2 * t;

  // Basis functions - compute once and reuse
  const float b0 = 0.5f * (-t3 + 2.0f * t2 - t); // -0.5*t^3 + t^2 - 0.5*t
  const float b1 =
      0.5f * (3.0f * t3 - 5.0f * t2 + 2.0f); // 1.5*t^3 - 2.5*t^2 + 1
  const float b2 =
      0.5f * (-3.0f * t3 + 4.0f * t2 + t); // -1.5*t^3 + 2*t^2 + 0.5*t
  const float b3 = 0.5f * (t3 - t2);       // 0.5*t^3 - 0.5*t^2

  // Direct calculation using basis functions
  return b0 * p0 + b1 * p1 + b2 * p2 + b3 * p3;
}

void bindVerticesToSpine(const Spline &spine,
                         const std::vector<glm::vec3> &vertices) {
  // Create frames along the spine
  const int numFrames = 50;
  std::vector<SplineFrame> frames(numFrames);

  // Compute frames
  frames[0] = spine.computeFrame(0.0f);
  for (int i = 1; i < numFrames; ++i) {
    float t = static_cast<float>(i) / (numFrames - 1) * spine.getLength();
    frames[i] = spine.evaluateFrame(t, frames[i - 1]);
  }

  // Resize binding array
  vertexBindings.resize(vertices.size());

  // Bind each vertex to closest frame
  for (size_t i = 0; i < vertices.size(); ++i) {
    const glm::vec3 &vertex = vertices[i];
    float minDist = FLT_MAX;
    int closestFrameIndex = 0;

    // Find closest frame
    for (size_t j = 0; j < frames.size(); ++j) {
      const SplineFrame &frame = frames[j];
      float dist = glm::distance(vertex, frame.position);

      if (dist < minDist) {
        minDist = dist;
        closestFrameIndex = j;
      }
    }

    // Calculate local coordinates in frame space
    const SplineFrame &frame = frames[closestFrameIndex];
    glm::vec3 toVertex = vertex - frame.position;

    float x = glm::dot(toVertex, frame.tangent);
    float y = glm::dot(toVertex, frame.normal);
    float z = glm::dot(toVertex, frame.binormal);

    // Store binding
    vertexBindings[i].frame_index = closestFrameIndex;
    vertexBindings[i].local_coords = glm::vec3(x, y, z);
  }
}
