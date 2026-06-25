#include "TerrainSystem.hpp"
#include <algorithm>
#include <array>
#include <limits>

namespace {

float nearestPointDistance(const glm::vec2 &point, const glm::vec2 &center, const float halfSize) {
    const glm::vec2 minBounds = center - glm::vec2(halfSize);
    const glm::vec2 maxBounds = center + glm::vec2(halfSize);
    const glm::vec2 clamped = glm::clamp(point, minBounds, maxBounds);
    return glm::length(point - clamped);
}

} // namespace

TerrainSystem::TerrainSystem(TerrainConfig config) : config(std::move(config)) {}

float TerrainSystem::nearestDistance(const TerrainChunk &chunk, const glm::vec2 &cameraXY) const {
    return nearestPointDistance(cameraXY, chunk.center, chunk.size * 0.5f);
}

void TerrainSystem::update(const glm::vec3 &cameraPosition) {
    if (!root) {
        root = std::make_unique<TerrainChunk>(config.origin, config.rootSize, 0);
    }

    decideShape(*root, cameraPosition);
    enforceBalance();

    patches.clear();
    collectPatches(*root);
}

void TerrainSystem::decideShape(TerrainChunk &chunk, const glm::vec3 &cameraPosition) {
    const glm::vec2 cameraXY(cameraPosition.x, cameraPosition.y);
    const float distance = nearestDistance(chunk, cameraXY);
    const bool wantsSplit = chunk.depth < config.maxDepth && distance < chunk.size * config.splitFactor;

    if (wantsSplit) {
        ensureChildren(chunk);
        for (auto &child : chunk.children) {
            decideShape(*child, cameraPosition);
        }
    } else {
        collapseChildren(chunk);
    }
}

void TerrainSystem::ensureChildren(TerrainChunk &chunk) {
    if (chunk.isSplit()) {
        return;
    }

    const float childSize = chunk.size * 0.5f;
    const float childOffset = childSize * 0.5f;

    static constexpr std::array<glm::vec2, 4> childDirections = {glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f),
                                                                 glm::vec2(-1.0f, 1.0f), glm::vec2(1.0f, 1.0f)};

    for (size_t i = 0; i < chunk.children.size(); ++i) {
        const glm::vec2 childCenter = chunk.center + childDirections[i] * childOffset;
        chunk.children[i] = std::make_unique<TerrainChunk>(childCenter, childSize, chunk.depth + 1);
    }
}

void TerrainSystem::collapseChildren(TerrainChunk &chunk) {
    for (auto &child : chunk.children) {
        child = nullptr;
    }
}

const TerrainChunk *TerrainSystem::findLeafContaining(const glm::vec2 &point) const {
    if (!root) {
        return nullptr;
    }

    const float halfRoot = root->size * 0.5f;
    if (point.x < root->center.x - halfRoot || point.x > root->center.x + halfRoot || point.y < root->center.y - halfRoot ||
        point.y > root->center.y + halfRoot) {
        return nullptr;
    }

    const TerrainChunk *node = root.get();
    while (node->isSplit()) {
        const TerrainChunk *next = nullptr;
        for (const auto &child : node->children) {
            const float half = child->size * 0.5f;
            if (point.x >= child->center.x - half && point.x < child->center.x + half && point.y >= child->center.y - half &&
                point.y < child->center.y + half) {
                next = child.get();
                break;
            }
        }
        if (!next) {
            break;
        }
        node = next;
    }
    return node;
}

void TerrainSystem::enforceBalance() {
    if (!root) {
        return;
    }

    bool changed = true;
    int guard = 0;
    while (changed && guard <= config.maxDepth + 1) {
        changed = balancePass(*root);
        ++guard;
    }
}

bool TerrainSystem::balancePass(TerrainChunk &chunk) {
    if (chunk.isSplit()) {
        bool changed = false;
        for (auto &child : chunk.children) {
            changed = balancePass(*child) || changed;
        }
        return changed;
    }

    if (chunk.depth >= config.maxDepth) {
        return false;
    }

    static constexpr std::array<glm::vec2, 4> edgeDirections = {glm::vec2(1.0f, 0.0f), glm::vec2(-1.0f, 0.0f),
                                                                glm::vec2(0.0f, 1.0f), glm::vec2(0.0f, -1.0f)};

    const float probe = chunk.size * 0.5f + std::max(chunk.size * 0.001f, 0.01f);

    for (const auto &dir : edgeDirections) {
        const TerrainChunk *neighbor = findLeafContaining(chunk.center + dir * probe);
        if (neighbor && neighbor->depth > chunk.depth + 1) {
            ensureChildren(chunk);
            return true;
        }
    }

    return false;
}

void TerrainSystem::collectPatches(const TerrainChunk &chunk) {
    if (chunk.isSplit()) {
        for (const auto &child : chunk.children) {
            collectPatches(*child);
        }
        return;
    }

    TerrainPatchInstance patch;
    patch.origin = chunk.center - glm::vec2(chunk.size * 0.5f);

    if (chunk.depth == 0) {
        constexpr float noMorph = std::numeric_limits<float>::max();
        patch.params = glm::vec4(chunk.size, noMorph, noMorph, 0.0f);
    } else {
        const float nearThreshold = chunk.size * config.splitFactor;
        const float farThreshold = nearThreshold * 2.0f;
        const float morphEnd = farThreshold;
        const float morphStart = farThreshold - (farThreshold - nearThreshold) * config.morphRatio;
        patch.params = glm::vec4(chunk.size, morphStart, morphEnd, 0.0f);
    }

    patches.push_back(patch);
}
