#ifndef MATERIAL_BINDING_HPP
#define MATERIAL_BINDING_HPP

#pragma once
#include "Texture.hpp"

struct MaterialKey {
    std::shared_ptr<Texture> albedo;
    std::shared_ptr<Texture> normalMap;
    std::shared_ptr<Texture> ormMap;

    bool operator==(const MaterialKey &other) const = default;
};

struct MaterialKeyHash {
    size_t operator()(const MaterialKey &key) const noexcept {
        const size_t h1 = std::hash<std::shared_ptr<Texture>>{}(key.albedo);
        const size_t h2 = std::hash<std::shared_ptr<Texture>>{}(key.normalMap);
        const size_t h3 = std::hash<std::shared_ptr<Texture>>{}(key.ormMap);
        return ((h1 ^ (h2 << 1)) >> 1) ^ (h3 << 1);
    }
};

using MaterialDescriptorSets = std::unordered_map<MaterialKey, std::vector<vk::raii::DescriptorSet>, MaterialKeyHash>;

#endif
