#ifndef TERRAIN_CHUNK_HPP
#define TERRAIN_CHUNK_HPP

#pragma once
#include "RenderObject.hpp"
#include <array>
#include <memory>

struct TerrainEdgeReduction {
    bool top = false;
    bool bottom = false;
    bool left = false;
    bool right = false;

    bool operator==(const TerrainEdgeReduction &other) const = default;
};

class TerrainChunk {
  public:
    TerrainChunk(const glm::vec2 &center, float size, int depth, const glm::ivec2 &gridMin, int gridSize)
        : center(center), size(size), depth(depth), gridMin(gridMin), gridSize(gridSize) {}

    glm::vec2 center;
    float size;
    int depth;

    glm::ivec2 gridMin;
    int gridSize;

    RenderObjectHandle renderObjectHandle = 0;
    bool hasRenderObject = false;
    TerrainEdgeReduction appliedReduction;

    std::array<std::unique_ptr<TerrainChunk>, 4> children;

    [[nodiscard]] bool isSplit() const { return children[0] != nullptr; }
};

#endif
