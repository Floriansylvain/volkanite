#ifndef SUB_MESH_HPP
#define SUB_MESH_HPP

#pragma once
#include "Mesh.hpp"

struct SubMesh {
    std::string albedoFilename;
    std::string normalMapFilename;
    glm::vec3 baseColor{1.0f};
    float roughness = 0.5f;
    std::vector<Mesh::Vertex> vertices;
    std::vector<uint32_t> indices;
};

#endif
