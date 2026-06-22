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

    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
    for (const auto &v : vertices) {
        minBounds = glm::min(minBounds, v.pos);
        maxBounds = glm::max(maxBounds, v.pos);
    }
    boundingCenter = (minBounds + maxBounds) * 0.5f;

    float maxDistSq = 0.0f;
    for (const auto &v : vertices) {
        const glm::vec3 offset = v.pos - boundingCenter;
        maxDistSq = std::max(maxDistSq, glm::dot(offset, offset));
    }
    boundingRadius = std::sqrt(maxDistSq);
}
