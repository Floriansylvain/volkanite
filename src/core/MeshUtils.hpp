#ifndef MESH_UTILS_HPP
#define MESH_UTILS_HPP

#pragma once
#include "Mesh.hpp"
#include "SubMesh.hpp"
#include <string>

namespace MeshUtils {

void generateCube(Mesh &mesh, const float &size);

std::vector<SubMesh> loadFBXModel(const std::string &path);

} // namespace MeshUtils

#endif
