#ifndef CAMERA_HPP
#define CAMERA_HPP

#pragma once
#include <glm/gtc/matrix_transform.hpp>

class Camera {
  public:
    float x = 0.f, y = 0.f, z = 0.f, yaw = 0.f, pitch = 0.f;

    [[nodiscard]] glm::vec3 forward() const {
        const float yawRad = glm::radians(yaw);
        const float pitchRad = glm::radians(pitch);
        return glm::normalize(glm::vec3(cos(yawRad) * cos(pitchRad), sin(yawRad) * cos(pitchRad), sin(pitchRad)));
    }

    [[nodiscard]] glm::vec3 position() const { return {x, y, z}; }

    [[nodiscard]] glm::mat4 viewMatrix() const {
        return lookAt(position(), position() + forward(), glm::vec3(0.0f, 0.0f, 1.0f));
    }

    [[nodiscard]] static glm::mat4 projMatrix(const float aspect, const float fovDegrees = 55.0f) {
        constexpr float near = 0.1f;
        constexpr float far = 2'000.0f;
        glm::mat4 proj = glm::perspective(glm::radians(fovDegrees), aspect, near, far);
        proj[1][1] *= -1;
        return proj;
    }
};

#endif
