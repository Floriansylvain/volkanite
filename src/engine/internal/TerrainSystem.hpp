#ifndef TERRAIN_SYSTEM_HPP
#define TERRAIN_SYSTEM_HPP

#pragma once
#include "TerrainChunk.hpp"
#include "TerrainTypes.hpp"
#include <memory>

class Engine;

class TerrainSystem {
  public:
    TerrainSystem(Engine &engine, TerrainConfig config);

    void update(const glm::vec3 &cameraPosition);

  private:
    Engine &engine;
    TerrainConfig config;

    std::unique_ptr<TerrainChunk> root;

    void decideShape(TerrainChunk &chunk, const glm::vec3 &cameraPosition);
    void ensureChildren(TerrainChunk &chunk);
    void collapseChildren(TerrainChunk &chunk);
    void destroySubtree(TerrainChunk &chunk);

    void enforceBalance();
    bool balancePass(TerrainChunk &chunk);
    [[nodiscard]] const TerrainChunk *findLeafContaining(const glm::vec2 &point) const;

    void syncRenderObjects(TerrainChunk &chunk);
    [[nodiscard]] TerrainEdgeReduction computeEdgeReduction(const TerrainChunk &chunk) const;

    void generateChunkRenderObject(TerrainChunk &chunk, const TerrainEdgeReduction &reduction);
    void removeChunkRenderObject(TerrainChunk &chunk);
};

#endif
