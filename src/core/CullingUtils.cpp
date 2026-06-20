#include "CullingUtils.hpp"

CullingUtils::Frustum CullingUtils::extractFrustum(const glm::mat4 &viewProj) {
    const glm::vec4 row0(viewProj[0][0], viewProj[1][0], viewProj[2][0], viewProj[3][0]);
    const glm::vec4 row1(viewProj[0][1], viewProj[1][1], viewProj[2][1], viewProj[3][1]);
    const glm::vec4 row2(viewProj[0][2], viewProj[1][2], viewProj[2][2], viewProj[3][2]);
    const glm::vec4 row3(viewProj[0][3], viewProj[1][3], viewProj[2][3], viewProj[3][3]);

    Frustum frustum{};
    frustum.planes[0] = row3 + row0; // left
    frustum.planes[1] = row3 - row0; // right
    frustum.planes[2] = row3 + row1; // bottom
    frustum.planes[3] = row3 - row1; // top
    frustum.planes[4] = row2;        // near
    frustum.planes[5] = row3 - row2; // far

    for (auto &p : frustum.planes) {
        p /= glm::length(glm::vec3(p));
    }
    return frustum;
}

bool CullingUtils::sphereInFrustum(const Frustum &frustum, const glm::vec3 &center, const float radius) {
    return std::ranges::all_of(frustum.planes, [center, radius](const auto &plane) {
        return glm::dot(glm::vec3(plane), center) + plane.w >= -radius;
    });
}
