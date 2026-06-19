#include "Mesh.hpp"

#include "VulkanUtils.hpp"

Mesh::Mesh(const VulkanContext &context) : vkCtx(context) {}

void Mesh::generateCube(const float size) {
    float half = size / 2.0f;

    vertices.clear();
    indices.clear();
    vertices.reserve(24);
    indices.reserve(36);

    struct FaceDef {
        glm::vec3 normal;
        glm::vec3 color;
        glm::vec3 v0;
        glm::vec3 v1;
        glm::vec3 v2;
        glm::vec3 v3;
    };

    const std::array<FaceDef, 6> faces = {
        {{{0, 0, 1}, {1, 0, 0}, {-half, -half, half}, {half, -half, half}, {half, half, half}, {-half, half, half}},
         {{0, 0, -1}, {0, 1, 0}, {half, -half, -half}, {-half, -half, -half}, {-half, half, -half}, {half, half, -half}},
         {{0, 1, 0}, {0, 0, 1}, {-half, half, half}, {half, half, half}, {half, half, -half}, {-half, half, -half}},
         {{0, -1, 0}, {1, 1, 0}, {-half, -half, -half}, {half, -half, -half}, {half, -half, half}, {-half, -half, half}},
         {{1, 0, 0}, {0, 1, 1}, {half, -half, half}, {half, -half, -half}, {half, half, -half}, {half, half, half}},
         {{-1, 0, 0}, {1, 0, 1}, {-half, -half, -half}, {-half, -half, half}, {-half, half, half}, {-half, half, -half}}}};

    uint16_t vertexOffset = 0;

    for (const auto &face : faces) {
        vertices.push_back({face.v0, face.color, {0.0f, 0.0f}});
        vertices.push_back({face.v1, face.color, {1.0f, 0.0f}});
        vertices.push_back({face.v2, face.color, {1.0f, 1.0f}});
        vertices.push_back({face.v3, face.color, {0.0f, 1.0f}});

        indices.push_back(vertexOffset + 0);
        indices.push_back(vertexOffset + 1);
        indices.push_back(vertexOffset + 2);

        indices.push_back(vertexOffset + 2);
        indices.push_back(vertexOffset + 3);
        indices.push_back(vertexOffset + 0);

        vertexOffset += 4;
    }
}

void Mesh::createGeometryBuffers(const vk::raii::CommandPool &commandPool) {
    using enum vk::BufferUsageFlagBits;
    using enum vk::MemoryPropertyFlagBits;

    const vk::DeviceSize vertexSize = sizeof(vertices[0]) * vertices.size();
    const vk::DeviceSize indexSize = sizeof(indices[0]) * indices.size();
    const vk::DeviceSize totalBufferSize = vertexSize + indexSize;

    auto [stagingBuffer, stagingBufferMemory] =
        VulkanUtils::createBuffer(vkCtx, totalBufferSize, eTransferSrc, eHostVisible | eHostCoherent);

    auto *dataStaging = static_cast<uint8_t *>(stagingBufferMemory.mapMemory(0, totalBufferSize));

    std::memcpy(dataStaging, vertices.data(), vertexSize);

    std::memcpy(dataStaging + vertexSize, indices.data(), indexSize);

    stagingBufferMemory.unmapMemory();

    std::tie(unifiedBuffer, unifiedBufferMemory) =
        VulkanUtils::createBuffer(vkCtx, totalBufferSize, eVertexBuffer | eIndexBuffer | eTransferDst, eDeviceLocal);

    VulkanUtils::copyBuffer(vkCtx, stagingBuffer, unifiedBuffer, totalBufferSize, commandPool);
}
