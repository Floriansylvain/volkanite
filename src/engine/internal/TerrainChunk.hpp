#ifndef TERRAIN_CHUNK_HPP
#define TERRAIN_CHUNK_HPP

class TerrainChunk {
  public:
    TerrainChunk(const glm::vec2 &center, float size, int depth) : center(center), size(size), depth(depth) {}

    glm::vec2 center;
    float size;
    int depth;

    std::array<std::unique_ptr<TerrainChunk>, 4> children;

    [[nodiscard]] bool isSplit() const { return children[0] != nullptr; }
};

#endif
