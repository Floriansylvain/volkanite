#ifndef TERRAIN_SYSTEM_HPP
#define TERRAIN_SYSTEM_HPP

#pragma once
#include "TerrainChunk.hpp"
#include "TerrainTypes.hpp"
#include <memory>

class TerrainSystem {
  public:
    explicit TerrainSystem(TerrainConfig config);

    void update(const glm::vec3 &cameraPosition);

    [[nodiscard]] const std::vector<TerrainPatchInstance> &activePatches() const { return patches; }
    [[nodiscard]] const std::vector<TerrainPatchInstance> &activeFinePatches() const { return finePatches; }

  private:
    TerrainConfig config;

    std::unique_ptr<TerrainChunk> root;
    std::vector<TerrainPatchInstance> patches;
    std::vector<TerrainPatchInstance> finePatches;

    [[nodiscard]] float nearestDistance(const TerrainChunk &chunk, const glm::vec2 &cameraXY) const;

    void decideShape(TerrainChunk &chunk, const glm::vec3 &cameraPosition);
    void ensureChildren(TerrainChunk &chunk);
    void collapseChildren(TerrainChunk &chunk);

    void enforceBalance();
    bool balancePass(TerrainChunk &chunk);
    [[nodiscard]] const TerrainChunk *findLeafContaining(const glm::vec2 &point) const;

    void collectPatches(const TerrainChunk &chunk);
};

#endif
