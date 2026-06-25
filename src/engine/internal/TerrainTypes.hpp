#ifndef TERRAIN_TYPES_HPP
#define TERRAIN_TYPES_HPP

#pragma once
#include "Material.hpp"

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

struct TerrainConfig {
    glm::vec2 origin{0.0f, 0.0f};
    float rootSize = 2048.0f;
    int maxDepth = 6;
    int chunkResolution = 33;
    int fineChunkResolution = 0;
    float splitFactor = 2.0f;
    float morphRatio = 0.3f;
    float textureWorldScale = 8.0f;
    glm::vec2 uvScale{1.0f, 1.0f};
    TerrainNoiseSettings noise;
    std::vector<TerrainMaterialLayer> materialLayers;
};

struct TerrainPatchInstance {
    glm::vec2 origin;
    glm::vec4 params;
};

#endif
