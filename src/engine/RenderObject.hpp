#ifndef RENDER_OBJECT_HPP
#define RENDER_OBJECT_HPP

#pragma once
#include "Mesh.hpp"
#include "Texture.hpp"

using RenderObjectHandle = size_t;

struct RenderObject {
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Texture> texture;
    bool isVisible = true;
};

#endif
