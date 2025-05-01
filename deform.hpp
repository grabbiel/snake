#pragma once
#include "spline.hpp"

#include <glm/vec3.hpp>
#include <vector>
void deformMeshToSpline(const std::vector<glm::vec3> &restVertices,
                        std::vector<glm::vec3> &outVertices,
                        const Spline &spine);
glm::vec3 catmullRom(const glm::vec3 &P0, const glm::vec3 &P1,
                     const glm::vec3 &P2, const glm::vec3 &P3, float t);
glm::mat3 parallelTransport(const glm::mat3 &prevFrame,
                            const glm::vec3 &prevTangent,
                            const glm::vec3 &currTangent);
