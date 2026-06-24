#ifndef MESH_UTILS_HPP
#define MESH_UTILS_HPP

#pragma once
#include "Mesh.hpp"
#include "SubMesh.hpp"
#include "ufbx.h"

#include <string>

namespace MeshUtils {

void generateCube(Mesh &mesh, const float &size);
void generateTerrain(Mesh &mesh, int width, int depth, float spacing, float scale, float heightScale, int octaves,
                     float persistence, float lacunarity);

void extractMaterial(const ufbx_mesh *mesh, const ufbx_mesh_part &part, SubMesh &sub);
void computeTangents(std::vector<Mesh::Vertex> &vertices, const std::vector<uint32_t> &indices);
Mesh::Vertex processVertex(const ufbx_mesh *mesh, const ufbx_node *node, uint32_t corner);
SubMesh processMeshPart(const ufbx_mesh *mesh, const ufbx_node *node, const ufbx_mesh_part &part);
void processMesh(const ufbx_mesh *mesh, std::vector<SubMesh> &subMeshes);
std::vector<SubMesh> loadFBXModel(const std::string &path);

void deduplicateVertices(std::vector<Mesh::Vertex> &vertices, std::vector<uint32_t> &indices);

} // namespace MeshUtils

#endif
