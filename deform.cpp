#include "deform.hpp"
#include "glm/geometric.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/norm.hpp"
#include "globals.hpp"

// Improved mesh deformation function
void deformMeshToSpline(const std::vector<glm::vec3> &restVertices,
                        std::vector<glm::vec3> &outVertices,
                        const Spline &spine) {
  // Create more frames for better coverage
  int numFrames = 100;

  // Check if frames need to be recomputed
  if (spineControlPointsChanged || cachedSpineFrames.size() != numFrames) {
    // Resize cache if needed
    cachedSpineFrames.resize(numFrames);

    // Compute all frames along the spine
    cachedSpineFrames[0] = spine.computeFrame(0.0f);
    for (int i = 1; i < numFrames; ++i) {
      float t = static_cast<float>(i) / (numFrames - 1) * spine.getLength();
      cachedSpineFrames[i] = spine.evaluateFrame(t, cachedSpineFrames[i - 1]);
    }

    // Clear the dirty flag
    spineControlPointsChanged = false;

    // Store control points for comparison
    previousControlPoints = spine.getControlPoints();
  }

  // Apply deformation to all vertices using cached frames
  for (size_t i = 0; i < vertexBindings.size(); ++i) {
    const VertexBinding &binding = vertexBindings[i];

    // Ensure frame index is valid
    int frameIdx = binding.frame_index;
    if (frameIdx >= cachedSpineFrames.size()) {
      frameIdx = cachedSpineFrames.size() - 1;
    }

    const SplineFrame &frame = cachedSpineFrames[frameIdx];

    // Transform using local coordinates
    outVertices[i] = frame.position + frame.tangent * binding.local_coords.x +
                     frame.normal * binding.local_coords.y +
                     frame.binormal * binding.local_coords.z;
  }
}

// Rotational minimizing frame update
// Optimized parallel transport calculation
glm::mat3 parallelTransport(const glm::mat3 &prevFrame,
                            const glm::vec3 &prevTangent,
                            const glm::vec3 &currTangent) {
  // Use previously normalized tangents and compute dot product
  float cosTheta = glm::dot(prevTangent, currTangent);

  // Fast path for nearly identical tangents (common case)
  if (cosTheta > 0.9999f) {
    return prevFrame; // No rotation needed
  }

  // Fast path for opposite tangents (rare but needs handling)
  if (cosTheta < -0.9999f) {
    // Use normal from previous frame as rotation axis
    const glm::vec3 &axis = prevFrame[0];

    // Optimized 180° rotation calculation (pre-computed form)
    float x2 = axis.x * axis.x * 2.0f;
    float y2 = axis.y * axis.y * 2.0f;
    float z2 = axis.z * axis.z * 2.0f;
    float xy = axis.x * axis.y * 2.0f;
    float xz = axis.x * axis.z * 2.0f;
    float yz = axis.y * axis.z * 2.0f;

    glm::mat3 R(x2 - 1.0f, xy, xz, xy, y2 - 1.0f, yz, xz, yz, z2 - 1.0f);

    return R * prevFrame;
  }

  // Calculate rotation axis
  glm::vec3 axis = glm::cross(prevTangent, currTangent);
  float sinTheta = glm::length(axis);

  // Handle numerical precision issues
  if (sinTheta < 1e-6f) {
    return prevFrame;
  }

  // Fast normalization
  axis /= sinTheta;

  // Precompute components for rotation matrix
  float t = 1.0f - cosTheta;
  float tx = t * axis.x;
  float ty = t * axis.y;
  float tz = t * axis.z;
  float sx = sinTheta * axis.x;
  float sy = sinTheta * axis.y;
  float sz = sinTheta * axis.z;

  // Direct calculation of Rodrigues rotation matrix (optimized)
  glm::mat3 R(cosTheta + tx * axis.x, tx * axis.y - sz, tx * axis.z + sy,
              tx * axis.y + sz, cosTheta + ty * axis.y, ty * axis.z - sx,
              tx * axis.z - sy, ty * axis.z + sx, cosTheta + tz * axis.z);

  return R * prevFrame;
}
