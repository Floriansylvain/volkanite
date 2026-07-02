#ifndef TERRAIN_HEIGHTMAP_BAKER_HPP
#define TERRAIN_HEIGHTMAP_BAKER_HPP

#pragma once
#include "TerrainTypes.hpp"
#include <cstdint>
#include <vector>

struct TerrainHeightmapData {
    int resolution = 0;
    std::vector<float> heights;
    std::vector<uint8_t> normals;
};

TerrainHeightmapData bakeTerrainChunkHeightmap(const TerrainNoiseSettings &noise, glm::vec2 chunkCenter, float chunkWorldSize,
                                               int resolution);

#endif
