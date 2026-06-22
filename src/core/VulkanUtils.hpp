#ifndef VULKAN_UTILS_HPP
#define VULKAN_UTILS_HPP

namespace VulkanUtils {
using ImageAllocation = std::pair<vk::raii::Image, vk::raii::DeviceMemory>;
using BufferAllocation = std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>;

uint32_t findMemoryType(const VulkanContext &vkCtx, uint32_t typeFilter, vk::MemoryPropertyFlags properties);

struct CreateImageCommand {
    uint32_t width;
    uint32_t height;
    uint32_t mipLevels;
    vk::SampleCountFlagBits samples;
    vk::Format format;
    vk::ImageTiling tiling;
    vk::ImageUsageFlags usage;
    vk::MemoryPropertyFlags properties;
};

struct ImageBarrierCommand {
    vk::Image image;
    vk::ImageLayout old_layout;
    vk::ImageLayout new_layout;
    vk::AccessFlags2 src_access_mask;
    vk::AccessFlags2 dst_access_mask;
    vk::PipelineStageFlags2 src_stage_mask;
    vk::PipelineStageFlags2 dst_stage_mask;
    vk::ImageAspectFlags image_aspect_flags;
    uint32_t base_mip_level = 0;
    uint32_t level_count = 1;
};

void imageBarriers(const vk::raii::CommandBuffer &commandBuffer, const std::vector<ImageBarrierCommand> &commands);

ImageAllocation createImage(const VulkanContext &vkCtx, const CreateImageCommand &command);

vk::raii::ImageView createImageView(const VulkanContext &vkCtx, const vk::Image &image, vk::Format format,
                                    vk::ImageAspectFlags aspectFlags, uint32_t mipLevels, uint32_t baseMipLevel = 0);

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

[[nodiscard]] vk::raii::DescriptorSetLayout
createDescriptorSetLayout(const VulkanContext &vkCtx, const std::vector<vk::DescriptorSetLayoutBinding> &bindings);

[[nodiscard]] vk::raii::PipelineLayout createPipelineLayout(const VulkanContext &vkCtx,
                                                            const std::vector<vk::DescriptorSetLayout> &setLayouts,
                                                            const std::vector<vk::PushConstantRange> &pushConstantRanges = {});

} // namespace VulkanUtils

#endif