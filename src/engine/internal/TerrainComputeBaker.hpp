#ifndef TERRAIN_COMPUTE_BAKER_HPP
#define TERRAIN_COMPUTE_BAKER_HPP

#pragma once
#include "TerrainTypes.hpp"

class TerrainComputeBaker {
  public:
    explicit TerrainComputeBaker(VulkanContext &vkCtx);

    void createResources(int maxPaddedResolution, int expectedConcurrentBakes);
    void createPipelines();

    struct BakeCommand {
        const vk::raii::CommandBuffer *commandBuffer;
        glm::vec2 chunkMin;
        float texelWorldSize;
        int resolution;
        TerrainNoiseSettings noise;
        vk::ImageView outHeightView;
        vk::Image outHeightImage;
        vk::ImageView outNormalView;
        vk::Image outNormalImage;
    };

    struct BakeResources {
        vk::raii::Image paddedHeightImage = nullptr;
        vk::raii::DeviceMemory paddedHeightMemory = nullptr;
        vk::raii::ImageView paddedHeightView = nullptr;
        vk::raii::DescriptorSet heightPassSet = nullptr;
        vk::raii::DescriptorSet normalPassSet = nullptr;
    };

    [[nodiscard]] BakeResources dispatchBake(const BakeCommand &command) const;

  private:
    struct TerrainBakePushConstants {
        glm::vec2 chunkMin;
        glm::vec2 noiseOffset;
        float texelWorldSize;
        int paddedResolution;
        float noiseScale;
        float heightScale;
        float baseHeight;
        float persistence;
        float lacunarity;
        int octaves;
        float ridgeSharpness;
        float heightRedistribution;
        float regionScale;
        float regionThreshold;
        float regionBlendWidth;
        float flatScale;
        float flatThreshold;
        float flatBlendWidth;
        float minRelief;
        float unusedPadding = 0.0f;
    };

    struct TerrainBakeResolvePushConstants {
        int resolution;
        float texelWorldSize;
    };

    void createDescriptorSetLayouts();
    [[nodiscard]] BakeResources allocateBakeResources() const;

    VulkanContext &vkCtx;

    int maxPaddedResolution = 0;

    vk::raii::DescriptorSetLayout heightPassSetLayout = nullptr;
    vk::raii::DescriptorSetLayout normalPassSetLayout = nullptr;
    vk::raii::PipelineLayout heightPassPipelineLayout = nullptr;
    vk::raii::PipelineLayout normalPassPipelineLayout = nullptr;
    vk::raii::Pipeline heightPassPipeline = nullptr;
    vk::raii::Pipeline normalPassPipeline = nullptr;

    vk::raii::DescriptorPool descriptorPool = nullptr;
};

#endif
