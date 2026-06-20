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

    void createGeometryBuffers(const vk::raii::CommandPool &commandPool);

    vk::raii::Buffer unifiedBuffer = nullptr;
    vk::raii::DeviceMemory unifiedBufferMemory = nullptr;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    float boundingRadius = 0.0f;

  private:
    const VulkanContext &vkCtx;
};

#endif