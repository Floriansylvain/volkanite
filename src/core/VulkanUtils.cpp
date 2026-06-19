#include "VulkanUtils.hpp"
#include "Exceptions.hpp"

vk::raii::ImageView VulkanUtils::createImageView(VulkanContext const &vkCtx, vk::Image const &image, const vk::Format format,
                                                 const vk::ImageAspectFlags aspectFlags) {
    vk::ImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = aspectFlags;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = 1;

    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = image;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = subresourceRange;

    return {vkCtx.device, viewInfo};
}

uint32_t VulkanUtils::findMemoryType(VulkanContext const &vkCtx, const uint32_t typeFilter,
                                     const vk::MemoryPropertyFlags properties) {
    const vk::PhysicalDeviceMemoryProperties memProperties = vkCtx.physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if (typeFilter & 1 << i && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw EngineExceptions::Compatibility("Failed to find suitable memory type.");
}

std::pair<vk::raii::Image, vk::raii::DeviceMemory>
VulkanUtils::createImage(VulkanContext const &vkCtx, const uint32_t width, const uint32_t height, vk::Format format,
                         vk::ImageTiling tiling, const vk::ImageUsageFlags usage, const vk::MemoryPropertyFlags properties) {
    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.format = format;
    imageInfo.extent = vk::Extent3D{width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.tiling = tiling;
    imageInfo.usage = usage;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;

    auto image = vk::raii::Image(vkCtx.device, imageInfo);

    const vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(vkCtx, memRequirements.memoryTypeBits, properties);

    auto imageMemory = vk::raii::DeviceMemory(vkCtx.device, allocInfo);
    image.bindMemory(imageMemory, 0);

    return {std::move(image), std::move(imageMemory)};
}

std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> VulkanUtils::createBuffer(const VulkanContext &vkCtx,
                                                                              const vk::DeviceSize size,
                                                                              const vk::BufferUsageFlags usage,
                                                                              const vk::MemoryPropertyFlags properties) {
    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    auto buffer = vk::raii::Buffer(vkCtx.device, bufferInfo);

    const vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(vkCtx, memRequirements.memoryTypeBits, properties);

    auto bufferMemory = vk::raii::DeviceMemory(vkCtx.device, allocInfo);

    buffer.bindMemory(*bufferMemory, 0);

    return {std::move(buffer), std::move(bufferMemory)};
}

void VulkanUtils::copyBuffer(const VulkanContext &vkCtx, const vk::raii::Buffer &srcBuffer, const vk::raii::Buffer &dstBuffer,
                             const vk::DeviceSize size, const vk::raii::CommandPool &commandPool) {
    vk::raii::CommandBuffer commandCopyBuffer = beginSingleTimeCommands(vkCtx, commandPool);
    commandCopyBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy(0, 0, size));
    endSingleTimeCommands(vkCtx, std::move(commandCopyBuffer));
}

vk::raii::CommandBuffer VulkanUtils::beginSingleTimeCommands(const VulkanContext &vkCtx,
                                                             const vk::raii::CommandPool &commandPool) {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = 1;

    vk::raii::CommandBuffer commandBuffer = std::move(vk::raii::CommandBuffers(vkCtx.device, allocInfo).front());

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    commandBuffer.begin(beginInfo);

    return std::move(commandBuffer);
}

void VulkanUtils::endSingleTimeCommands(const VulkanContext &vkCtx, vk::raii::CommandBuffer &&commandBuffer) {
    commandBuffer.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &*commandBuffer;

    vkCtx.queue.submit(submitInfo, nullptr);
    vkCtx.queue.waitIdle();
}
