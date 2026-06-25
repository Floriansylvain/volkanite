#ifndef TERRAIN_PATCH_RENDERER_HPP
#define TERRAIN_PATCH_RENDERER_HPP

#pragma once
#include "Mesh.hpp"
#include "PerFrameBuffer.hpp"
#include "TerrainTypes.hpp"

class TerrainPatchRenderer {
  public:
    explicit TerrainPatchRenderer(VulkanContext &vkCtx, int maxFramesInFlight);

    void createGridMeshes(const vk::raii::CommandPool &commandPool, const TerrainConfig &config);
    void createPipelines(vk::DescriptorSetLayout materialSetLayout, vk::Format colorFormat, vk::Format depthFormat,
                         vk::Format shadowDepthFormat);

    void setPatches(std::vector<TerrainPatchInstance> coarsePatches, std::vector<TerrainPatchInstance> finePatches);
    void upload(uint32_t frameIndex);

    struct DrawCommand {
        vk::raii::CommandBuffer *commandBuffer;
        vk::DescriptorSet materialSet;
        uint32_t frameIndex;
    };
    void draw(const DrawCommand &command, bool isWireframe, uint32_t &drawCallCount) const;
    void drawShadow(const DrawCommand &command) const;

    uint64_t getVisibleVertexEstimate() const;

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
        float morphSnapStride;
    };

    struct Tier {
        std::shared_ptr<Mesh> mesh;
        PerFrameBuffer instanceBuffer;
        std::vector<TerrainPatchInstance> pendingPatches;
        uint32_t activeInstanceCount = 0;
        float gridDim = 0.0f;
        float morphSnapStride = 2.0f;
    };

    void buildTierMesh(Tier &tier, const vk::raii::CommandPool &commandPool, int resolution);
    void uploadTier(Tier &tier, uint32_t frameIndex);
    void drawTier(const Tier &tier, vk::Pipeline pipeline, const DrawCommand &command) const;

    VulkanContext &vkCtx;
    int maxFramesInFlight;

    Tier coarseTier;
    Tier fineTier;

    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline solidPipeline = nullptr;
    vk::raii::Pipeline wireframePipeline = nullptr;
    vk::raii::Pipeline shadowPipeline = nullptr;

    TerrainPushConstants pushConstants{};
};

#endif
