#include "TerrainSystem.hpp"
#include "Engine.hpp"
#include "MeshUtils.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <glm/gtc/noise.hpp>

namespace {

float sampleTerrainHeight(const TerrainNoiseSettings &noise, const glm::vec2 &worldPos) {
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float h = 0.0f;
    const glm::vec2 base = worldPos / noise.scale + noise.offset;

    for (int i = 0; i < noise.octaves; ++i) {
        h += glm::perlin(base * frequency) * amplitude;
        amplitude *= noise.persistence;
        frequency *= noise.lacunarity;
    }

    return h * noise.heightScale + noise.baseHeight;
}

std::vector<int> buildFullRangeIndices(const int last, const bool reduced) {
    std::vector<int> result;
    if (!reduced) {
        for (int i = 0; i <= last; ++i) {
            result.push_back(i);
        }
        return result;
    }
    for (int i = 0; i <= last; i += 2) {
        result.push_back(i);
    }
    return result;
}

std::vector<int> buildInnerRangeIndices(const int last, const bool reduced) {
    std::vector<int> result;
    const int start = 1;
    const int end = last - 1;
    if (end < start) {
        return result;
    }
    if (end == start) {
        result.push_back(start);
        return result;
    }
    if (!reduced) {
        for (int i = start; i <= end; ++i) {
            result.push_back(i);
        }
        return result;
    }
    result.push_back(start);
    for (int i = start + 1; i <= end - 1; i += 2) {
        result.push_back(i);
    }
    result.push_back(end);
    return result;
}

void emitBorderStrip(std::vector<uint32_t> &indices, const std::vector<int> &kept, const std::function<uint32_t(int)> &outer,
                     const std::function<uint32_t(int)> &inner, const bool flip) {
    for (size_t k = 0; k + 1 < kept.size(); ++k) {
        const int i0 = kept[k];
        const int i1 = kept[k + 1];

        if (i1 - i0 <= 1) {
            const uint32_t o0 = outer(i0), o1 = outer(i1);
            const uint32_t n0 = inner(i0), n1 = inner(i1);
            if (!flip) {
                indices.push_back(o0);
                indices.push_back(o1);
                indices.push_back(n0);
                indices.push_back(o1);
                indices.push_back(n1);
                indices.push_back(n0);
            } else {
                indices.push_back(o0);
                indices.push_back(n0);
                indices.push_back(o1);
                indices.push_back(o1);
                indices.push_back(n0);
                indices.push_back(n1);
            }
        } else {
            const int iMid = i0 + (i1 - i0) / 2;
            const uint32_t o0 = outer(i0), o1 = outer(i1);
            const uint32_t n0 = inner(i0), nm = inner(iMid), n1 = inner(i1);

            const uint32_t patterns[2][9] = {{o0, o1, nm, o0, nm, n0, o1, n1, nm}, {o0, nm, o1, o0, n0, nm, o1, nm, n1}};

            const auto &p = patterns[flip ? 1 : 0];
            indices.insert(indices.end(), std::begin(p), std::end(p));
        }
    }
}

} // namespace

TerrainSystem::TerrainSystem(Engine &engine, TerrainConfig config) : engine(engine), config(std::move(config)) {}

void TerrainSystem::update(const glm::vec3 &cameraPosition) {
    if (!root) {
        const int quadsPerChunk = config.chunkResolution - 1;
        const int rootGridSize = quadsPerChunk * (1 << config.maxDepth);

        root = std::make_unique<TerrainChunk>(config.origin, config.rootSize, 0, glm::ivec2(0, 0), rootGridSize);
    }

    decideShape(*root, cameraPosition);
    enforceBalance();
    syncRenderObjects(*root);
}

void TerrainSystem::decideShape(TerrainChunk &chunk, const glm::vec3 &cameraPosition) {
    const glm::vec2 cameraXY(cameraPosition.x, cameraPosition.y);
    const float distance = glm::length(cameraXY - chunk.center);
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

    removeChunkRenderObject(chunk);

    const float childSize = chunk.size * 0.5f;
    const float childOffset = childSize * 0.5f;
    const int childGridSize = chunk.gridSize / 2;

    static constexpr std::array<glm::vec2, 4> childDirections = {glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f),
                                                                 glm::vec2(-1.0f, 1.0f), glm::vec2(1.0f, 1.0f)};

    const std::array<glm::ivec2, 4> gridOffsets = {glm::ivec2(0, 0), glm::ivec2(childGridSize, 0), glm::ivec2(0, childGridSize),
                                                   glm::ivec2(childGridSize, childGridSize)};

    for (size_t i = 0; i < chunk.children.size(); ++i) {
        const glm::vec2 childCenter = chunk.center + childDirections[i] * childOffset;
        const glm::ivec2 childGridMin = chunk.gridMin + gridOffsets[i];

        chunk.children[i] =
            std::make_unique<TerrainChunk>(childCenter, childSize, chunk.depth + 1, childGridMin, childGridSize);
    }
}

void TerrainSystem::collapseChildren(TerrainChunk &chunk) {
    if (!chunk.isSplit()) {
        return;
    }
    for (auto &child : chunk.children) {
        destroySubtree(*child);
        child = nullptr;
    }
}

void TerrainSystem::destroySubtree(TerrainChunk &chunk) {
    if (chunk.isSplit()) {
        for (auto &child : chunk.children) {
            destroySubtree(*child);
        }
    }
    removeChunkRenderObject(chunk);
}

void TerrainSystem::removeChunkRenderObject(TerrainChunk &chunk) {
    if (chunk.hasRenderObject) {
        engine.removeRenderObject(chunk.renderObjectHandle);
        chunk.hasRenderObject = false;
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

TerrainEdgeReduction TerrainSystem::computeEdgeReduction(const TerrainChunk &chunk) const {
    const float probe = chunk.size * 0.5f + std::max(chunk.size * 0.001f, 0.01f);

    const auto coarserThanMe = [&](const glm::vec2 &point) {
        const TerrainChunk *neighbor = findLeafContaining(point);
        return neighbor != nullptr && neighbor->depth < chunk.depth;
    };

    TerrainEdgeReduction reduction;
    reduction.top = coarserThanMe(glm::vec2(chunk.center.x, chunk.center.y - probe));
    reduction.bottom = coarserThanMe(glm::vec2(chunk.center.x, chunk.center.y + probe));
    reduction.left = coarserThanMe(glm::vec2(chunk.center.x - probe, chunk.center.y));
    reduction.right = coarserThanMe(glm::vec2(chunk.center.x + probe, chunk.center.y));
    return reduction;
}

void TerrainSystem::syncRenderObjects(TerrainChunk &chunk) {
    if (chunk.isSplit()) {
        for (auto &child : chunk.children) {
            syncRenderObjects(*child);
        }
        return;
    }

    const TerrainEdgeReduction current = computeEdgeReduction(chunk);
    if (!chunk.hasRenderObject || !(current == chunk.appliedReduction)) {
        removeChunkRenderObject(chunk);
        generateChunkRenderObject(chunk, current);
        chunk.appliedReduction = current;
    }
}

void TerrainSystem::generateChunkRenderObject(TerrainChunk &chunk, const TerrainEdgeReduction &reduction) {
    int resolution = std::max(config.chunkResolution, 5);
    if (resolution % 2 == 0) {
        resolution += 1;
    }
    const int last = resolution - 1;

    const int quadsPerChunk = resolution - 1;
    const int rootGridSize = quadsPerChunk * (1 << config.maxDepth);
    const float worldUnitsPerGrid = config.rootSize / static_cast<float>(rootGridSize);

    const glm::vec2 rootOrigin = config.origin - glm::vec2(config.rootSize * 0.5f);
    const int gridStep = chunk.gridSize / quadsPerChunk;
    const float step = static_cast<float>(gridStep) * worldUnitsPerGrid;
    const float half = chunk.size * 0.5f;

    std::vector<float> heights(static_cast<size_t>(resolution) * static_cast<size_t>(resolution));
    for (int y = 0; y < resolution; ++y) {
        for (int x = 0; x < resolution; ++x) {
            const int gridX = chunk.gridMin.x + x * gridStep;
            const int gridY = chunk.gridMin.y + y * gridStep;

            const float worldX = rootOrigin.x + static_cast<float>(gridX) * worldUnitsPerGrid;
            const float worldY = rootOrigin.y + static_cast<float>(gridY) * worldUnitsPerGrid;

            heights[y * resolution + x] = sampleTerrainHeight(config.noise, glm::vec2(worldX, worldY));
        }
    }

    if (reduction.left) {
        heights[1 * resolution + 0] = (heights[0 * resolution + 0] + heights[2 * resolution + 0]) * 0.5f;
        heights[(last - 1) * resolution + 0] = (heights[(last - 2) * resolution + 0] + heights[last * resolution + 0]) * 0.5f;
    }

    if (reduction.right) {
        heights[1 * resolution + last] = (heights[0 * resolution + last] + heights[2 * resolution + last]) * 0.5f;
        heights[(last - 1) * resolution + last] =
            (heights[(last - 2) * resolution + last] + heights[last * resolution + last]) * 0.5f;
    }

    std::vector<glm::vec3> normals(heights.size());

    const float normalEpsilon = 1.0f;
    const float inv2Eps = 0.5f / normalEpsilon;

    for (int y = 0; y < resolution; ++y) {
        for (int x = 0; x < resolution; ++x) {
            const float worldX = chunk.center.x - half + static_cast<float>(x) * step;
            const float worldY = chunk.center.y - half + static_cast<float>(y) * step;

            const float hL = sampleTerrainHeight(config.noise, glm::vec2(worldX - normalEpsilon, worldY));
            const float hR = sampleTerrainHeight(config.noise, glm::vec2(worldX + normalEpsilon, worldY));
            const float hD = sampleTerrainHeight(config.noise, glm::vec2(worldX, worldY - normalEpsilon));
            const float hU = sampleTerrainHeight(config.noise, glm::vec2(worldX, worldY + normalEpsilon));

            normals[y * resolution + x] = glm::normalize(glm::vec3((hL - hR) * inv2Eps, (hD - hU) * inv2Eps, 1.0f));
        }
    }

    std::vector<Mesh::Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(heights.size());
    indices.reserve(heights.size() * 6);

    for (int y = 0; y < resolution; ++y) {
        for (int x = 0; x < resolution; ++x) {
            const int gridX = chunk.gridMin.x + x * gridStep;
            const int gridY = chunk.gridMin.y + y * gridStep;

            const float worldX = rootOrigin.x + static_cast<float>(gridX) * worldUnitsPerGrid;
            const float worldY = rootOrigin.y + static_cast<float>(gridY) * worldUnitsPerGrid;

            const float height = heights[y * resolution + x];
            const glm::vec3 &normal = normals[y * resolution + x];

            Mesh::Vertex vertex{};
            vertex.pos = glm::vec3(worldX, worldY, height);
            vertex.color = glm::vec3(1.0f);
            vertex.texCoord = glm::vec2(worldX, worldY) / config.textureWorldScale;
            vertex.normal = normal;
            vertices.push_back(vertex);
        }
    }

    const auto vertexIndex = [resolution](const int x, const int y) { return static_cast<uint32_t>(y * resolution + x); };

    emitBorderStrip(
        indices, buildFullRangeIndices(last, reduction.top), [&](const int x) { return vertexIndex(x, 0); },
        [&](const int x) { return vertexIndex(x, 1); }, false);

    emitBorderStrip(
        indices, buildFullRangeIndices(last, reduction.bottom), [&](const int x) { return vertexIndex(x, last); },
        [&](const int x) { return vertexIndex(x, last - 1); }, true);

    emitBorderStrip(
        indices, buildInnerRangeIndices(last, reduction.left), [&](const int y) { return vertexIndex(0, y); },
        [&](const int y) { return vertexIndex(1, y); }, true);

    emitBorderStrip(
        indices, buildInnerRangeIndices(last, reduction.right), [&](const int y) { return vertexIndex(last, y); },
        [&](const int y) { return vertexIndex(last - 1, y); }, false);

    for (int y = 1; y <= last - 2; ++y) {
        for (int x = 1; x <= last - 2; ++x) {
            const uint32_t topLeft = vertexIndex(x, y);
            const uint32_t topRight = vertexIndex(x + 1, y);
            const uint32_t bottomLeft = vertexIndex(x, y + 1);
            const uint32_t bottomRight = vertexIndex(x + 1, y + 1);

            indices.push_back(topLeft);
            indices.push_back(topRight);
            indices.push_back(bottomLeft);

            indices.push_back(topRight);
            indices.push_back(bottomRight);
            indices.push_back(bottomLeft);
        }
    }

    MeshUtils::computeTangents(vertices, indices);

    auto mesh = engine.createMeshFromData(std::move(vertices), std::move(indices));

    RenderObject object;
    object.mesh = mesh;
    object.mesh->uvScale = glm::vec2(16.f);
    object.material = config.material;
    object.isVisible = true;

    chunk.renderObjectHandle = engine.addRenderObject(std::move(object));
    chunk.hasRenderObject = true;
}
