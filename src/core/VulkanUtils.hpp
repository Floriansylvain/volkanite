#ifndef VULKAN_UTILS_HPP
#define VULKAN_UTILS_HPP

#pragma once
#include "VulkanContext.hpp"
#include <vulkan/vulkan_raii.hpp>

namespace VulkanUtils {
using ImageAllocation = std::pair<vk::raii::Image, vk::raii::DeviceMemory>;
using BufferAllocation = std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>;

uint32_t findMemoryType(const VulkanContext &vkCtx, uint32_t typeFilter, vk::MemoryPropertyFlags properties);

ImageAllocation createImage(const VulkanContext &vkCtx, uint32_t width, uint32_t height, vk::Format format,
                            vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties);

vk::raii::ImageView createImageView(const VulkanContext &vkCtx, const vk::Image &image, vk::Format format,
                                    vk::ImageAspectFlags aspectFlags);

BufferAllocation createBuffer(const VulkanContext &vkCtx, vk::DeviceSize size, vk::BufferUsageFlags usage,
                              vk::MemoryPropertyFlags properties);
} // namespace VulkanUtils

#endif