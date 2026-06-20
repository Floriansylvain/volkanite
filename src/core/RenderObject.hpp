#ifndef RENDER_OBJECT_HPP
#define RENDER_OBJECT_HPP

#pragma once
#include "Mesh.hpp"
#include "Texture.hpp"
#include <memory>

struct RenderObject {
    glm::vec3 position{0.0f};
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Texture> texture;
    bool isInstanced = false;
    bool isVisible = true;
    float rotationSpeed = glm::radians(90.0f);
};

#endif
