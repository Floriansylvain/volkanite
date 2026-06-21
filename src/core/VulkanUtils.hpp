#ifndef VULKAN_UTILS_HPP
#define VULKAN_UTILS_HPP

#pragma once
#include "VulkanContext.hpp"
#include <vulkan/vulkan_raii.hpp>

namespace VulkanUtils {
using ImageAllocation = std::pair<vk::raii::Image, vk::raii::DeviceMemory>;
using BufferAllocation = std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>;

uint32_t findMemoryType(const VulkanContext &vkCtx, uint32_t typeFilter, vk::MemoryPropertyFlags properties);

struct CreateImageCommand {
    uint32_t width;
    uint32_t height;
    uint32_t mipLevels;
    vk::Format format;
    vk::ImageTiling tiling;
    vk::ImageUsageFlags usage;
    vk::MemoryPropertyFlags properties;
};

ImageAllocation createImage(const VulkanContext &vkCtx, CreateImageCommand command);

vk::raii::ImageView createImageView(const VulkanContext &vkCtx, const vk::Image &image, vk::Format format,
                                    vk::ImageAspectFlags aspectFlags, uint32_t mipLevels);

BufferAllocation createBuffer(const VulkanContext &vkCtx, vk::DeviceSize size, vk::BufferUsageFlags usage,
                              vk::MemoryPropertyFlags properties);

void copyBuffer(const VulkanContext &vkCtx, const vk::raii::Buffer &srcBuffer, const vk::raii::Buffer &dstBuffer,
                vk::DeviceSize size, const vk::raii::CommandPool &commandPool);

[[nodiscard]] vk::raii::CommandBuffer beginSingleTimeCommands(const VulkanContext &vkCtx,
                                                              const vk::raii::CommandPool &commandPool);

void endSingleTimeCommands(const VulkanContext &vkCtx, const vk::raii::CommandBuffer &commandBuffer);

void transitionImageLayout(const vk::raii::CommandBuffer &commandBuffer, const vk::raii::Image &image,
                           vk::ImageLayout oldLayout, vk::ImageLayout newLayout, uint32_t mipLevels);
void copyBufferToImage(const vk::raii::CommandBuffer &commandBuffer, const vk::raii::Buffer &buffer,
                       const vk::raii::Image &image, uint32_t width, uint32_t height);

} // namespace VulkanUtils

#endif