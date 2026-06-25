#ifndef TERRAIN_PATCH_RENDERER_HPP
#define TERRAIN_PATCH_RENDERER_HPP

#pragma once
#include "Mesh.hpp"
#include "PerFrameBuffer.hpp"
#include "TerrainTypes.hpp"

class TerrainPatchRenderer {
  public:
    explicit TerrainPatchRenderer(VulkanContext &vkCtx, int maxFramesInFlight);

    void createGridMesh(const vk::raii::CommandPool &commandPool, const TerrainConfig &config);
    void createPipelines(vk::DescriptorSetLayout materialSetLayout, vk::Format colorFormat, vk::Format depthFormat,
                         vk::Format shadowDepthFormat);

    void setPatches(std::vector<TerrainPatchInstance> patches);
    void upload(uint32_t frameIndex);

    struct DrawCommand {
        vk::raii::CommandBuffer *commandBuffer;
        vk::DescriptorSet materialSet;
        uint32_t frameIndex;
    };
    void draw(const DrawCommand &command, bool isWireframe, uint32_t &drawCallCount) const;
    void drawShadow(const DrawCommand &command) const;

  private:
    static constexpr size_t MAX_PATCHES = 4096;

    struct TerrainPushConstants {
        glm::vec2 noiseOffset;
        float noiseScale;
        float heightScale;
        float baseHeight;
        float persistence;
        float lacunarity;
        int octaves;
        float textureWorldScale;
        float gridDim;
        float uvScaleX;
        float uvScaleY;
        float ridgeSharpness;
        float heightRedistribution;
        float regionScale;
        float regionThreshold;
        float regionBlendWidth;
    };

    VulkanContext &vkCtx;
    int maxFramesInFlight;

    std::shared_ptr<Mesh> gridMesh;

    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline solidPipeline = nullptr;
    vk::raii::Pipeline wireframePipeline = nullptr;
    vk::raii::Pipeline shadowPipeline = nullptr;

    PerFrameBuffer instanceBuffers;
    std::vector<TerrainPatchInstance> pendingPatches;
    uint32_t activeInstanceCount = 0;

    TerrainPushConstants pushConstants{};
};

#endif
