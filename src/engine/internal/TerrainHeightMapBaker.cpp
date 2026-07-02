#include "TerrainHeightmapBaker.hpp"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace {

glm::vec2 gradient(const glm::vec2 &p) {
    const glm::vec2 hashed(glm::dot(p, glm::vec2(127.1f, 311.7f)), glm::dot(p, glm::vec2(269.5f, 183.3f)));
    const glm::vec2 s(std::sin(hashed.x), std::sin(hashed.y));
    return -1.0f + 2.0f * glm::fract(s * 43758.5453123f);
}

float quinticFade(const float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float gradientNoise2D(const glm::vec2 &p) {
    const glm::vec2 i = glm::floor(p);
    const glm::vec2 f = glm::fract(p);

    const glm::vec2 g00 = gradient(i + glm::vec2(0.0f, 0.0f));
    const glm::vec2 g10 = gradient(i + glm::vec2(1.0f, 0.0f));
    const glm::vec2 g01 = gradient(i + glm::vec2(0.0f, 1.0f));
    const glm::vec2 g11 = gradient(i + glm::vec2(1.0f, 1.0f));

    const float v00 = glm::dot(g00, f - glm::vec2(0.0f, 0.0f));
    const float v10 = glm::dot(g10, f - glm::vec2(1.0f, 0.0f));
    const float v01 = glm::dot(g01, f - glm::vec2(0.0f, 1.0f));
    const float v11 = glm::dot(g11, f - glm::vec2(1.0f, 1.0f));

    const float u = quinticFade(f.x);
    const float v = quinticFade(f.y);

    const float x1 = glm::mix(v00, v10, u);
    const float x2 = glm::mix(v01, v11, u);
    return glm::mix(x1, x2, v);
}

float regionMask(const glm::vec2 &worldPos, const TerrainNoiseSettings &noise) {
    const glm::vec2 base = worldPos / std::max(noise.regionScale, 1.0f);
    const float n = glm::clamp(gradientNoise2D(base) * 0.5f + 0.5f, 0.0f, 1.0f);
    const float halfWidth = std::max(noise.regionBlendWidth, 0.001f);
    return glm::smoothstep(noise.regionThreshold - halfWidth, noise.regionThreshold + halfWidth, n);
}

float reliefMask(const glm::vec2 &worldPos, const TerrainNoiseSettings &noise) {
    const glm::vec2 base = worldPos / std::max(noise.flatScale, 1.0f) + glm::vec2(91.7f, -57.3f);
    const float n = glm::clamp(gradientNoise2D(base) * 0.5f + 0.5f, 0.0f, 1.0f);
    const float halfWidth = std::max(noise.flatBlendWidth, 0.001f);
    return glm::smoothstep(noise.flatThreshold - halfWidth, noise.flatThreshold + halfWidth, n);
}

float terrainHeight(const glm::vec2 &worldPos, const TerrainNoiseSettings &noise) {
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float hills = 0.0f;
    float mountains = 0.0f;
    const glm::vec2 base = worldPos / noise.scale + noise.offset;

    const float sharpness = glm::clamp(noise.ridgeSharpness, 0.0f, 1.0f);
    const float ridgeExponent = glm::mix(1.0f, 6.0f, sharpness);

    for (int i = 0; i < noise.octaves; i++) {
        const float n = gradientNoise2D(base * frequency);
        const float ridgeShape = std::pow(glm::clamp(1.0f - std::abs(n), 0.0f, 1.0f), ridgeExponent) * 2.0f - 1.0f;

        hills += n * amplitude;
        mountains += glm::mix(n, ridgeShape, sharpness) * amplitude;

        amplitude *= noise.persistence;
        frequency *= noise.lacunarity;
    }

    const float mountainAmount = regionMask(worldPos, noise);
    const float combined = glm::mix(hills, mountains, mountainAmount);

    const float relief = glm::mix(glm::clamp(noise.minRelief, 0.0f, 1.0f), 1.0f, reliefMask(worldPos, noise));
    const float shaped = combined * relief;

    const float signH = shaped < 0.0f ? -1.0f : 1.0f;
    const float h = signH * std::pow(std::abs(shaped), std::max(noise.heightRedistribution, 0.0001f));

    return h * noise.heightScale + noise.baseHeight;
}

} // namespace

TerrainHeightmapData bakeTerrainChunkHeightmap(const TerrainNoiseSettings &noise, const glm::vec2 chunkCenter,
                                               const float chunkWorldSize, const int requestedResolution) {
    TerrainHeightmapData data;
    const int resolution = std::max(requestedResolution, 2);
    data.resolution = resolution;
    data.heights.resize(static_cast<size_t>(resolution) * resolution);
    data.normals.resize(static_cast<size_t>(resolution) * resolution * 4);

    const float halfSize = chunkWorldSize * 0.5f;
    const float texelWorldSize = chunkWorldSize / static_cast<float>(resolution - 1);
    const glm::vec2 chunkMin = chunkCenter - glm::vec2(halfSize);

    const int paddedRes = resolution + 2;
    std::vector<float> padded(static_cast<size_t>(paddedRes) * paddedRes);

    for (int y = 0; y < paddedRes; ++y) {
        for (int x = 0; x < paddedRes; ++x) {
            const glm::vec2 worldXY = chunkMin + glm::vec2(x - 1, y - 1) * texelWorldSize;
            padded[static_cast<size_t>(y) * paddedRes + x] = terrainHeight(worldXY, noise);
        }
    }

    const auto paddedAt = [&](const int x, const int y) { return padded[static_cast<size_t>(y + 1) * paddedRes + (x + 1)]; };

    for (int y = 0; y < resolution; ++y) {
        for (int x = 0; x < resolution; ++x) {
            data.heights[static_cast<size_t>(y) * resolution + x] = paddedAt(x, y);

            const float hL = paddedAt(x - 1, y);
            const float hR = paddedAt(x + 1, y);
            const float hD = paddedAt(x, y - 1);
            const float hU = paddedAt(x, y + 1);

            const glm::vec3 normal =
                glm::normalize(glm::vec3((hL - hR) / (2.0f * texelWorldSize), (hD - hU) / (2.0f * texelWorldSize), 1.0f));

            const size_t idx = (static_cast<size_t>(y) * resolution + x) * 4;
            data.normals[idx + 0] = static_cast<uint8_t>((normal.x * 0.5f + 0.5f) * 255.0f);
            data.normals[idx + 1] = static_cast<uint8_t>((normal.y * 0.5f + 0.5f) * 255.0f);
            data.normals[idx + 2] = static_cast<uint8_t>((normal.z * 0.5f + 0.5f) * 255.0f);
            data.normals[idx + 3] = 255;
        }
    }

    return data;
}
