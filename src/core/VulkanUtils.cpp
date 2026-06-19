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
