#ifndef CULLING_UTILS_HPP
#define CULLING_UTILS_HPP

#pragma once
#include <glm/glm.hpp>

namespace CullingUtils {

struct Frustum {
    glm::vec4 planes[6];
};

Frustum extractFrustum(const glm::mat4 &viewProj);

bool sphereInFrustum(const Frustum &frustum, const glm::vec3 &center, float radius);

} // namespace CullingUtils

#endif
