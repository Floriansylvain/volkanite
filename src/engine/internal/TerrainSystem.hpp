#ifndef TERRAIN_SYSTEM_HPP
#define TERRAIN_SYSTEM_HPP

#pragma once
#include "TerrainTypes.hpp"
#include <deque>
#include <unordered_map>
#include <vector>

struct TerrainGenerationRequest {
    TerrainChunkCoord coord;
    glm::vec2 worldOrigin;
    float worldSize = 0.0f;
    int lodIndex = 0;
};

class TerrainSystem {
  public:
    explicit TerrainSystem(TerrainConfig config);

    void update(const glm::vec3 &cameraPosition, const glm::vec3 &viewDirection);

    [[nodiscard]] std::vector<TerrainGenerationRequest> takeNextRequests(int maxCount);
    [[nodiscard]] std::vector<TerrainChunkCoord> takeChunksToUnload();

  private:
    [[nodiscard]] TerrainChunkCoord chunkCoordAt(const glm::vec2 &worldXY) const;
    [[nodiscard]] glm::vec2 chunkCenter(const TerrainChunkCoord &coord) const;
    [[nodiscard]] int pickLod(float distanceChunks) const;
    [[nodiscard]] static float requestPriority(const TerrainGenerationRequest &request, const glm::vec2 &cameraXY,
                                               const glm::vec2 &viewDirXY);
    void purgePendingRequestsFor(const TerrainChunkCoord &coord);

    TerrainConfig config;

    std::deque<TerrainGenerationRequest> pendingRequests;

    std::unordered_map<TerrainChunkCoord, int, TerrainChunkCoordHash> knownChunks;
    std::vector<TerrainChunkCoord> pendingUnloads;
};

#endif
