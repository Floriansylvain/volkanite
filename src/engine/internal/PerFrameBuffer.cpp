#include "PerFrameBuffer.hpp"
#include "VulkanUtils.hpp"

PerFrameBuffer::PerFrameBuffer(const VulkanContext &vkCtx, const uint32_t frameCount, const vk::DeviceSize size,
                               const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags properties) {
    const bool hostVisible = static_cast<bool>(properties & vk::MemoryPropertyFlagBits::eHostVisible);

    buffers.reserve(frameCount);
    memories.reserve(frameCount);
    mappedPtrs.reserve(frameCount);

    for (uint32_t i = 0; i < frameCount; i++) {
        auto [buffer, memory] = VulkanUtils::createBuffer(vkCtx, size, usage, properties);
        buffers.push_back(std::move(buffer));
        memories.push_back(std::move(memory));
        mappedPtrs.push_back(hostVisible ? memories.back().mapMemory(0, size) : nullptr);
    }
}

std::vector<vk::Buffer> PerFrameBuffer::handles() const {
    std::vector<vk::Buffer> result;
    result.reserve(buffers.size());
    for (const auto &b : buffers) {
        result.push_back(*b);
    }
    return result;
}
