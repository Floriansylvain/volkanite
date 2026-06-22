#ifndef MESH_HPP
#define MESH_HPP

#define GLM_ENABLE_EXPERIMENTAL

#pragma once
#include <array>
#include <glm/gtx/hash.hpp>

class Mesh {
  public:
    struct Vertex {
        glm::vec3 pos;
        glm::vec3 color;
        glm::vec2 texCoord;

        static vk::VertexInputBindingDescription getBindingDescription();
        static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions();

        bool operator==(const Vertex &other) const {
            return pos == other.pos && color == other.color && texCoord == other.texCoord;
        }
    };

    explicit Mesh(const VulkanContext &context);
    ~Mesh() = default;

    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;

    void createGeometryBuffers(const vk::raii::CommandPool &commandPool);

    vk::raii::Buffer unifiedBuffer = nullptr;
    vk::raii::DeviceMemory unifiedBufferMemory = nullptr;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    float boundingRadius = 0.0f;
    glm::vec3 boundingCenter{0.0f};

  private:
    const VulkanContext &vkCtx;
};

template <> struct std::hash<Mesh::Vertex> {
    size_t operator()(const Mesh::Vertex &vertex) const noexcept {
        const size_t h1 = hash<glm::vec3>()(vertex.pos);
        const size_t h2 = hash<glm::vec3>()(vertex.color);
        const size_t h3 = hash<glm::vec2>()(vertex.texCoord);
        return ((h1 ^ (h2 << 1)) >> 1) ^ (h3 << 1);
    }
}; // namespace std

#endif