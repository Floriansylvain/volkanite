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
    explicit OcclusionCuller(VulkanContext &context, int maxFramesInFlight);

    void createResources(vk::Extent2D extent, vk::Format depthFormat);
    void createPipelines(const vk::PipelineShaderStageCreateInfo &depthToMip0Stage,
                         const vk::PipelineShaderStageCreateInfo &downsampleStage);

    void createCullPipeline(const vk::PipelineShaderStageCreateInfo &cullStage);
    vk::raii::DescriptorSetLayout cullSetLayout = nullptr;
    vk::raii::PipelineLayout cullPipelineLayout = nullptr;
    vk::raii::Pipeline cullPipeline = nullptr;

    std::vector<vk::raii::Image> hiZImages;
    std::vector<vk::raii::ImageView> hiZFullViews;
    uint32_t mipLevels = 0;
    vk::raii::Sampler hiZSampler = nullptr;

    [[nodiscard]] vk::ImageView resolvedDepthView(uint32_t frameIndex) const { return *resolvedDepthImageViews[frameIndex]; }

    void buildPyramid(const vk::raii::CommandBuffer &commandBuffer, uint32_t frameIndex);
    void prepareDepthResolveTarget(const vk::raii::CommandBuffer &commandBuffer, uint32_t frameIndex);

    struct PyramidPushConstants {
        glm::ivec2 srcSize;
        glm::ivec2 dstSize;
        uint32_t instanceCount;
        float time;
        float boundingRadius;
        uint32_t maxMip;
        float boundingCenterX;
        float boundingCenterY;
        float boundingCenterZ;
        uint32_t occlusionEnabled;
    };

    void init();

  private:
    bool resourcesAllocated = false;
    bool poolCreated = false;

    struct MipLevelInfo {
        uint32_t width;
        uint32_t height;
    };

    VulkanContext &vkCtx;
    int maxFramesInFlight;

    vk::Extent2D extent{};
    std::vector<MipLevelInfo> mipExtents;

    std::vector<vk::raii::Image> resolvedDepthImages;
    std::vector<vk::raii::DeviceMemory> resolvedDepthImageMemories;
    std::vector<vk::raii::ImageView> resolvedDepthImageViews;
    vk::raii::Sampler depthSampler = nullptr;

    std::vector<vk::raii::DeviceMemory> hiZImageMemories;
    std::vector<std::vector<vk::raii::ImageView>> hiZMipViews;

    vk::raii::DescriptorSetLayout emptySetLayout = nullptr;
    vk::raii::DescriptorSetLayout mip0SetLayout = nullptr;
    vk::raii::DescriptorSetLayout downsampleSetLayout = nullptr;
    vk::raii::PipelineLayout mip0PipelineLayout = nullptr;
    vk::raii::PipelineLayout downsamplePipelineLayout = nullptr;
    vk::raii::Pipeline depthToMip0Pipeline = nullptr;
    vk::raii::Pipeline downsamplePipeline = nullptr;

    vk::raii::DescriptorPool descriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> mip0DescriptorSets;
    std::vector<std::vector<vk::raii::DescriptorSet>> downsampleDescriptorSets;

    void createDescriptorSetLayouts();
    void createDescriptorSets();
};

#endif
