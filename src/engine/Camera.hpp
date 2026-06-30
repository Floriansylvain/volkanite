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
        constexpr float far = 1'000'000.0f;
        glm::mat4 proj = glm::perspective(glm::radians(fovDegrees), aspect, near, far);
        proj[1][1] *= -1;
        return proj;
    }

    [[nodiscard]] static glm::mat4 lightViewProjMatrix(const glm::vec3 &lightDir, const glm::vec3 &center, const float distance,
                                                       const float halfExtent, const float nearPlane, const float farPlane) {
        glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
        if (glm::abs(glm::dot(up, lightDir)) > 0.999f) {
            up = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        const glm::vec3 eye = center + lightDir * distance;
        const glm::mat4 view = glm::lookAt(eye, center, up);

        glm::mat4 proj = glm::ortho(-halfExtent, halfExtent, -halfExtent, halfExtent, nearPlane, farPlane);
        proj[1][1] *= -1;

        return proj * view;
    }
};

#endif
