#ifndef TERRAIN_TYPES_HPP
#define TERRAIN_TYPES_HPP

#pragma once
#include "Material.hpp"
#include <cstdint>
#include <functional>

struct TerrainNoiseSettings {
    float scale = 150.0f;
    float heightScale = 40.0f;
    float baseHeight = -100.0f;
    int octaves = 7;
    float persistence = 0.5f;
    float lacunarity = 2.0f;
    glm::vec2 offset{0.0f, 0.0f};

    float ridgeSharpness = 0.6f;
    float heightRedistribution = 1.0f;

    float regionScale = 800.0f;
    float regionThreshold = 1.5f;
    float regionBlendWidth = 0.25f;

    float flatScale = 1500.0f;
    float flatThreshold = -1.5f;
    float flatBlendWidth = 0.25f;
    float minRelief = 0.12f;
};

struct TerrainMaterialLayer {
    Material material;
    float preferredHeight = 0.0f;
    float heightRange = 50.0f;
    float preferredSlope = 0.0f;
    float slopeRange = 0.5f;
};

struct TerrainLayerParamsGpu {
    float preferredHeight;
    float heightRange;
    float preferredSlope;
    float slopeRange;
};

struct TerrainChunkCoord {
    int32_t x = 0;
    int32_t y = 0;

    bool operator==(const TerrainChunkCoord &) const = default;
};

struct TerrainChunkCoordHash {
    size_t operator()(const TerrainChunkCoord &c) const noexcept {
        const auto ux = static_cast<uint64_t>(static_cast<uint32_t>(c.x));
        const auto uy = static_cast<uint64_t>(static_cast<uint32_t>(c.y));
        return std::hash<uint64_t>{}((ux << 32) | uy);
    }
};

struct TerrainLodLevel {
    float maxChunkDistance = 0.0f;
    int meshResolution = 65;
    int heightmapResolution = 129;
};

struct TerrainConfig {
    glm::vec2 origin{0.0f, 0.0f};
    float chunkWorldSize = 256.0f;
    int renderDistanceChunks = 8;
    int maxChunkUploadsPerFrame = 2;
    float textureWorldScale = 8.0f;
    glm::vec2 uvScale{1.0f, 1.0f};
    std::vector<TerrainLodLevel> lodLevels;
    float lodSkirtDepth = 48.0f;
    TerrainNoiseSettings noise;
    std::vector<TerrainMaterialLayer> materialLayers;
};

#endif
