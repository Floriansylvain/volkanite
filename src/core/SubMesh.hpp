#ifndef SUB_MESH_HPP
#define SUB_MESH_HPP

#pragma once
#include "Mesh.hpp"
#include <string>
#include <vector>

struct SubMesh {
    std::string filename;
    std::vector<Mesh::Vertex> vertices;
    std::vector<uint32_t> indices;
};

#endif
