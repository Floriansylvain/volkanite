#include "Mesh.hpp"

#include "VulkanUtils.hpp"

Mesh::Mesh(const VulkanContext &context) : vkCtx(context) {}

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

    float maxDistSq = 0.0f;
    for (const auto &v : vertices) {
        maxDistSq = std::max(maxDistSq, glm::dot(v.pos, v.pos));
    }
    boundingRadius = std::sqrt(maxDistSq);
}
