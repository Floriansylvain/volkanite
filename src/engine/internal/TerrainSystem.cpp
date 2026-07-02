#include "TerrainSystem.hpp"
#include "Exceptions.hpp"
#include <algorithm>
#include <cmath>

TerrainSystem::TerrainSystem(TerrainConfig config) : config(std::move(config)) {
    if (this->config.lodLevels.empty()) {
        throw EngineExceptions::Compatibility("TerrainConfig.lodLevels must contain at least one level");
    }
}

TerrainChunkCoord TerrainSystem::chunkCoordAt(const glm::vec2 &worldXY) const {
    const glm::vec2 local = (worldXY - config.origin) / config.chunkWorldSize;
    return TerrainChunkCoord{static_cast<int32_t>(std::floor(local.x)), static_cast<int32_t>(std::floor(local.y))};
}

glm::vec2 TerrainSystem::chunkCenter(const TerrainChunkCoord &coord) const {
    return config.origin +
           (glm::vec2(static_cast<float>(coord.x), static_cast<float>(coord.y)) + glm::vec2(0.5f)) * config.chunkWorldSize;
}

int TerrainSystem::pickLod(const float distanceChunks) const {
    for (size_t i = 0; i < config.lodLevels.size(); ++i) {
        if (distanceChunks <= config.lodLevels[i].maxChunkDistance) {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(config.lodLevels.size() - 1);
}

float TerrainSystem::requestPriority(const TerrainGenerationRequest &request, const glm::vec2 &cameraXY,
                                     const glm::vec2 &viewDirXY) {
    const glm::vec2 toChunk = request.worldOrigin - cameraXY;
    const float distance = glm::length(toChunk);

    float alignment = 0.0f;
    if (distance > 0.0001f && glm::length(viewDirXY) > 0.0001f) {
        alignment = glm::dot(toChunk / distance, viewDirXY);
    }

    constexpr float viewBias = 0.5f;
    return distance * (1.0f - alignment * viewBias);
}

void TerrainSystem::purgePendingRequestsFor(const TerrainChunkCoord &coord) {
    pendingRequests.erase(std::remove_if(pendingRequests.begin(), pendingRequests.end(),
                                         [&](const TerrainGenerationRequest &r) { return r.coord == coord; }),
                          pendingRequests.end());
}

void TerrainSystem::update(const glm::vec3 &cameraPosition, const glm::vec3 &viewDirection) {
    const glm::vec2 cameraXY(cameraPosition.x, cameraPosition.y);
    const TerrainChunkCoord cameraChunk = chunkCoordAt(cameraXY);

    const int loadRadius = std::max(config.renderDistanceChunks, 0);
    const int unloadRadius = loadRadius + 1;

    for (auto it = knownChunks.begin(); it != knownChunks.end();) {
        const float dx = static_cast<float>(it->first.x - cameraChunk.x);
        const float dy = static_cast<float>(it->first.y - cameraChunk.y);
        const float distChunks = std::sqrt(dx * dx + dy * dy);

        if (distChunks > static_cast<float>(unloadRadius)) {
            purgePendingRequestsFor(it->first);
            pendingUnloads.push_back(it->first);
            it = knownChunks.erase(it);
            continue;
        }

        if (pickLod(distChunks) != it->second) {
            purgePendingRequestsFor(it->first);
            it = knownChunks.erase(it);
            continue;
        }

        ++it;
    }

    for (int dy = -loadRadius; dy <= loadRadius; ++dy) {
        for (int dx = -loadRadius; dx <= loadRadius; ++dx) {
            if (dx * dx + dy * dy > loadRadius * loadRadius) {
                continue;
            }

            const TerrainChunkCoord coord{cameraChunk.x + dx, cameraChunk.y + dy};
            if (knownChunks.contains(coord)) {
                continue;
            }

            const float distChunks = std::sqrt(static_cast<float>(dx * dx + dy * dy));
            const int lod = pickLod(distChunks);
            knownChunks.emplace(coord, lod);
            pendingRequests.push_back(TerrainGenerationRequest{coord, chunkCenter(coord), config.chunkWorldSize, lod});
        }
    }

    const glm::vec2 viewDirXY(viewDirection.x, viewDirection.y);
    std::sort(pendingRequests.begin(), pendingRequests.end(),
              [&](const TerrainGenerationRequest &a, const TerrainGenerationRequest &b) {
                  return requestPriority(a, cameraXY, viewDirXY) < requestPriority(b, cameraXY, viewDirXY);
              });
}

std::vector<TerrainGenerationRequest> TerrainSystem::takeNextRequests(const int maxCount) {
    std::vector<TerrainGenerationRequest> result;

    const size_t count = std::min(pendingRequests.size(), static_cast<size_t>(std::max(maxCount, 0)));
    for (size_t i = 0; i < count; ++i) {
        const auto it = knownChunks.find(pendingRequests[i].coord);
        if (it != knownChunks.end() && it->second == pendingRequests[i].lodIndex) {
            result.push_back(pendingRequests[i]);
        }
    }
    pendingRequests.erase(pendingRequests.begin(), pendingRequests.begin() + static_cast<long>(count));

    return result;
}

std::vector<TerrainChunkCoord> TerrainSystem::takeChunksToUnload() {
    std::vector<TerrainChunkCoord> result = std::move(pendingUnloads);
    pendingUnloads.clear();
    return result;
}
