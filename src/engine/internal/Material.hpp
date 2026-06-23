#ifndef MATERIAL_HPP
#define MATERIAL_HPP

#pragma once
#include "Texture.hpp"

struct Material {
    std::shared_ptr<Texture> albedo;
    std::shared_ptr<Texture> normalMap;

    glm::vec3 baseColor{1.0f};
    float roughness = 0.5f;
};

#endif
