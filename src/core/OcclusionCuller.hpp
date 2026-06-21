#ifndef OCCLUSION_CULLER_HPP
#define OCCLUSION_CULLER_HPP

#pragma once
#include "VulkanContext.hpp"
#include <glm/glm.hpp>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

class OcclusionCuller {
  public:
    explicit OcclusionCuller(VulkanContext &context);

    void createResources(vk::Extent2D extent, vk::Format depthFormat);
    void createPipelines(const vk::PipelineShaderStageCreateInfo &depthToMip0Stage,
                         const vk::PipelineShaderStageCreateInfo &downsampleStage);

    [[nodiscard]] vk::ImageView resolvedDepthView() const { return *resolvedDepthImageView; }

    void buildPyramid(const vk::raii::CommandBuffer &commandBuffer);
    void prepareDepthResolveTarget(const vk::raii::CommandBuffer &commandBuffer);

  private:
    struct PyramidPushConstants {
        glm::ivec2 srcSize;
        glm::ivec2 dstSize;
    };

    struct MipLevelInfo {
        uint32_t width;
        uint32_t height;
    };

    VulkanContext &vkCtx;

    vk::Extent2D extent{};
    uint32_t mipLevels = 0;
    std::vector<MipLevelInfo> mipExtents;

    vk::raii::Image resolvedDepthImage = nullptr;
    vk::raii::DeviceMemory resolvedDepthImageMemory = nullptr;
    vk::raii::ImageView resolvedDepthImageView = nullptr;
    vk::raii::Sampler depthSampler = nullptr;

    vk::raii::Image hiZImage = nullptr;
    vk::raii::DeviceMemory hiZImageMemory = nullptr;
    std::vector<vk::raii::ImageView> hiZMipViews;

    vk::raii::DescriptorSetLayout emptySetLayout = nullptr;
    vk::raii::DescriptorSetLayout mip0SetLayout = nullptr;
    vk::raii::DescriptorSetLayout downsampleSetLayout = nullptr;
    vk::raii::PipelineLayout mip0PipelineLayout = nullptr;
    vk::raii::PipelineLayout downsamplePipelineLayout = nullptr;
    vk::raii::Pipeline depthToMip0Pipeline = nullptr;
    vk::raii::Pipeline downsamplePipeline = nullptr;

    vk::raii::DescriptorPool descriptorPool = nullptr;
    vk::raii::DescriptorSet mip0DescriptorSet = nullptr;
    std::vector<vk::raii::DescriptorSet> downsampleDescriptorSets;
    void createDescriptorSetLayouts();

    void createDescriptorSets();
};

#endif
