#ifndef MESH_HPP
#define MESH_HPP

#pragma once
#include "VulkanContext.hpp"
#include <array>
#include <glm/glm.hpp>
#include <vector>

class Mesh {
  public:
    struct Vertex {
        glm::vec3 pos;
        glm::vec3 color;
        glm::vec2 texCoord;

        static vk::VertexInputBindingDescription getBindingDescription();
        static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions();
    };

    explicit Mesh(const VulkanContext &context);
    ~Mesh() = default;

    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;

    void generateCube(float size);
    void createGeometryBuffers(const vk::raii::CommandPool &commandPool);

    vk::raii::Buffer unifiedBuffer = nullptr;
    vk::raii::DeviceMemory unifiedBufferMemory = nullptr;

    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

  private:
    const VulkanContext &vkCtx;
};

#endif