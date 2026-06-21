#include "VulkanUtils.hpp"
#include "Exceptions.hpp"

vk::raii::ImageView VulkanUtils::createImageView(VulkanContext const &vkCtx, vk::Image const &image, const vk::Format format,
                                                 const vk::ImageAspectFlags aspectFlags, uint32_t mipLevels) {
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
    viewInfo.subresourceRange.levelCount = mipLevels;

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

std::pair<vk::raii::Image, vk::raii::DeviceMemory> VulkanUtils::createImage(VulkanContext const &vkCtx,
                                                                            VulkanUtils::CreateImageCommand command) {
    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.format = command.format;
    imageInfo.extent = vk::Extent3D{command.width, command.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = command.samples;
    imageInfo.tiling = command.tiling;
    imageInfo.usage = command.usage;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.mipLevels = command.mipLevels;

    auto image = vk::raii::Image(vkCtx.device, imageInfo);

    const vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(vkCtx, memRequirements.memoryTypeBits, command.properties);

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
    const vk::raii::CommandBuffer commandCopyBuffer = beginSingleTimeCommands(vkCtx, commandPool);
    commandCopyBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy(0, 0, size));
    endSingleTimeCommands(vkCtx, commandCopyBuffer);
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

    return commandBuffer;
}

void VulkanUtils::endSingleTimeCommands(const VulkanContext &vkCtx, const vk::raii::CommandBuffer &commandBuffer) {
    commandBuffer.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &*commandBuffer;

    vkCtx.queue.submit(submitInfo, nullptr);
    vkCtx.queue.waitIdle();
}

void VulkanUtils::copyBufferToImage(const vk::raii::CommandBuffer &commandBuffer, const vk::raii::Buffer &buffer,
                                    const vk::raii::Image &image, const uint32_t width, const uint32_t height) {
    vk::ImageSubresourceLayers imageSubresourceLayers = {};
    imageSubresourceLayers.aspectMask = vk::ImageAspectFlagBits::eColor;
    imageSubresourceLayers.mipLevel = 0;
    imageSubresourceLayers.baseArrayLayer = 0;
    imageSubresourceLayers.layerCount = 1;

    vk::BufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = imageSubresourceLayers;
    region.imageOffset = vk::Offset3D(0, 0, 0);
    region.imageExtent = vk::Extent3D(width, height, 1);

    commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, region);
}

void VulkanUtils::transitionImageLayout(const vk::raii::CommandBuffer &commandBuffer, const vk::raii::Image &image,
                                        const vk::ImageLayout oldLayout, const vk::ImageLayout newLayout, uint32_t mipLevels) {
    vk::ImageSubresourceRange imageSubresourceRange = {};
    imageSubresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    imageSubresourceRange.levelCount = 1;
    imageSubresourceRange.layerCount = 1;

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.image = image;
    barrier.subresourceRange = imageSubresourceRange;
    barrier.subresourceRange.levelCount = mipLevels;

    vk::PipelineStageFlags sourceStage;
    vk::PipelineStageFlags destinationStage;

    if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
        destinationStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        sourceStage = vk::PipelineStageFlagBits::eTransfer;
        destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else {
        throw EngineExceptions::Compatibility("Unsupported layout transition.");
    }

    commandBuffer.pipelineBarrier(sourceStage, destinationStage, {}, {}, {}, barrier);
}
