#ifndef MATERIAL_HPP
#define MATERIAL_HPP

#pragma once
#include "Texture.hpp"

struct Material {
    std::shared_ptr<Texture> albedo;
    std::shared_ptr<Texture> normalMap;
    std::shared_ptr<Texture> roughnessMap;
    std::shared_ptr<Texture> metallicMap;

    glm::vec3 baseColor{1.0f};
    float roughness = 0.5f;
    float metallic = 0.0f;
};

#endif
