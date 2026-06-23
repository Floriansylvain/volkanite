#ifndef RENDER_OBJECT_HPP
#define RENDER_OBJECT_HPP

#pragma once
#include "Material.hpp"
#include "Mesh.hpp"

using RenderObjectHandle = size_t;

struct RenderObject {
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};
    std::shared_ptr<Mesh> mesh;
    Material material;
    bool isVisible = true;
};

#endif
