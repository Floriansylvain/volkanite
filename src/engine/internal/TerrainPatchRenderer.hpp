#ifndef TERRAIN_PATCH_RENDERER_HPP
#define TERRAIN_PATCH_RENDERER_HPP

#pragma once
#include "Mesh.hpp"
#include "PerFrameBuffer.hpp"
#include "TerrainTypes.hpp"

class TerrainPatchRenderer {
  public:
    explicit TerrainPatchRenderer(VulkanContext &vkCtx, int maxFramesInFlight);

    void createMaterialSetLayout();
    void createGridMeshes(const vk::raii::CommandPool &commandPool, const TerrainConfig &config);
    void createPipelines(vk::Format colorFormat, vk::Format depthFormat, vk::Format shadowDepthFormat);

    void setMaterialLayers(const std::vector<TerrainMaterialLayer> &layers, const std::vector<vk::Buffer> &cameraUniformBuffers,
                           vk::ImageView shadowMapView, vk::Sampler shadowSampler);

    void setPatches(std::vector<TerrainPatchInstance> coarsePatches, std::vector<TerrainPatchInstance> finePatches);
    void setViewBias(const glm::vec3 &cameraForward, const TerrainConfig &config);
    void upload(uint32_t frameIndex);

    struct DrawCommand {
        vk::raii::CommandBuffer *commandBuffer;
        uint32_t frameIndex;
    };
    void draw(const DrawCommand &command, bool isWireframe, uint32_t &drawCallCount) const;
    void drawShadow(const DrawCommand &command) const;

    uint64_t getVisibleVertexEstimate() const;

  private:
    static constexpr size_t MAX_PATCHES = 4096;
    static constexpr size_t MAX_MATERIAL_LAYERS = 4;

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
        float flatScale;
        float flatThreshold;
        float flatBlendWidth;
        float minRelief;
        int layerCount;
        float layer0PreferredHeight;
        float layer0HeightRange;
        float layer0PreferredSlope;
        float layer0SlopeRange;
        float layer1PreferredHeight;
        float layer1HeightRange;
        float layer1PreferredSlope;
        float layer1SlopeRange;
        float layer2PreferredHeight;
        float layer2HeightRange;
        float layer2PreferredSlope;
        float layer2SlopeRange;
        float layer3PreferredHeight;
        float layer3HeightRange;
        float layer3PreferredSlope;
        float layer3SlopeRange;

        int useViewBias = 1;
        float viewFullResRadius = 32.0f;
        float viewAheadMultiplier = 0.45f;
        float viewBehindMultiplier = 2.5f;
        float cameraForwardX = 1.0f;
        float cameraForwardY = 0.0f;

        float skirtDepthFactor = 0.02f;
        float skirtMinDepth = 0.25f;
        float skirtMaxDepth = 3.0f;
    };

    struct Tier {
        std::shared_ptr<Mesh> mesh;
        PerFrameBuffer instanceBuffer;
        std::vector<TerrainPatchInstance> pendingPatches;
        uint32_t activeInstanceCount = 0;
        float gridDim = 0.0f;
        float morphSnapStride = 2.0f;
    };

    void buildTierMesh(Tier &tier, const vk::raii::CommandPool &commandPool, int resolution) const;
    static void uploadTier(Tier &tier, uint32_t frameIndex);
    void drawTier(const Tier &tier, vk::Pipeline pipeline, const DrawCommand &command) const;

    VulkanContext &vkCtx;
    int maxFramesInFlight;

    Tier coarseTier;
    Tier fineTier;

    vk::raii::DescriptorSetLayout materialSetLayout = nullptr;
    vk::raii::DescriptorPool materialDescriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> materialSets;

    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline solidPipeline = nullptr;
    vk::raii::Pipeline wireframePipeline = nullptr;
    vk::raii::Pipeline shadowPipeline = nullptr;

    TerrainPushConstants pushConstants{};
};

#endif
