#ifndef TERRAIN_PATCH_RENDERER_HPP
#define TERRAIN_PATCH_RENDERER_HPP

#pragma once
#include "CullingUtils.hpp"
#include "Mesh.hpp"
#include "PerFrameBuffer.hpp"
#include "TerrainComputeBaker.hpp"
#include "TerrainTypes.hpp"
#include "Texture.hpp"
#include <unordered_map>

class TerrainPatchRenderer {
  public:
    explicit TerrainPatchRenderer(VulkanContext &vkCtx, int maxFramesInFlight);

    void createMaterialSetLayout();
    void createChunkMeshes(const vk::raii::CommandPool &commandPool, const TerrainConfig &config);
    void createPipelines(vk::Format colorFormat, vk::Format depthFormat, vk::Format shadowDepthFormat);

    void setMaterialLayers(const std::vector<TerrainMaterialLayer> &layers, const std::vector<vk::Buffer> &cameraUniformBuffers,
                           vk::ImageView shadowMapView, vk::Sampler shadowSampler);

    void addChunk(TerrainChunkCoord coord, glm::vec2 worldOrigin, float worldSize, int lodIndex,
                  const vk::raii::CommandBuffer &commandBuffer);
    void removeChunk(TerrainChunkCoord coord);
    void tickRetiredChunks();

    struct DrawCommand {
        vk::raii::CommandBuffer *commandBuffer;
        uint32_t frameIndex;
        const CullingUtils::Frustum *frustum;
    };
    void draw(const DrawCommand &command, bool isWireframe, uint32_t &drawCallCount) const;
    void drawShadow(const DrawCommand &command) const;

    uint64_t getVisibleVertexEstimate() const;

  private:
    static constexpr size_t MAX_MATERIAL_LAYERS = 4;

    struct TerrainPushConstants {
        glm::vec2 chunkOrigin;
        float chunkWorldSize;
        float textureWorldScale;
        float gridDim;
        float uvScaleX;
        float uvScaleY;
        float skirtDepth;
        int layerCount;
    };

    struct LodMesh {
        std::shared_ptr<Mesh> mesh;
        float gridDim = 0.0f;
    };

    struct ChunkGpuData {
        std::shared_ptr<Texture> heightMap;
        std::shared_ptr<Texture> normalMap;
        vk::raii::DescriptorSet chunkSet = nullptr;
        glm::vec2 worldOrigin{0.0f};
        float worldSize = 0.0f;
        int lodIndex = 0;
    };

    struct RetiringChunk {
        ChunkGpuData data;
        int framesRemaining = 0;
    };

    struct PendingBakeResources {
        TerrainComputeBaker::BakeResources resources;
        int framesRemaining = 0;
    };

    void drawChunk(const ChunkGpuData &chunk, vk::Pipeline pipeline, const DrawCommand &command) const;
    [[nodiscard]] bool isChunkVisible(const ChunkGpuData &chunk, const CullingUtils::Frustum &frustum) const;

    VulkanContext &vkCtx;
    int maxFramesInFlight;

    std::vector<LodMesh> lodMeshes;
    std::vector<TerrainLodLevel> lodLevels;
    TerrainNoiseSettings noiseSettings;
    float verticalCullExtent = 0.0f;

    TerrainComputeBaker computeBaker;

    vk::raii::DescriptorSetLayout materialSetLayout = nullptr;
    vk::raii::DescriptorPool materialDescriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> materialSets;

    vk::raii::DescriptorSetLayout chunkSetLayout = nullptr;
    vk::raii::DescriptorPool chunkDescriptorPool = nullptr;
    uint32_t chunkDescriptorPoolCapacity = 0;

    PerFrameBuffer layerParamsBuffer;

    std::unordered_map<TerrainChunkCoord, ChunkGpuData, TerrainChunkCoordHash> chunks;
    std::vector<RetiringChunk> retiringChunks;
    std::vector<PendingBakeResources> pendingBakeResources;

    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline solidPipeline = nullptr;
    vk::raii::Pipeline wireframePipeline = nullptr;
    vk::raii::Pipeline shadowPipeline = nullptr;

    TerrainPushConstants pushConstants{};
    mutable uint64_t lastVisibleIndexCount = 0;
};

#endif
