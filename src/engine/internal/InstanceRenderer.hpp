#ifndef INSTANCE_RENDERER_HPP
#define INSTANCE_RENDERER_HPP

#pragma once
#include "CullingUtils.hpp"
#include "MaterialBinding.hpp"
#include "Mesh.hpp"
#include "PerFrameBuffer.hpp"
#include "PipelineBuilder.hpp"
#include "RenderObject.hpp"
#include "Texture.hpp"
#include <unordered_map>
#include <unordered_set>

class InstanceRenderer {
  public:
    explicit InstanceRenderer(VulkanContext &vkCtx, int maxFramesInFlight);

    void createPipelines(vk::PipelineLayout pipelineLayout, vk::Format colorFormat, vk::Format depthFormat,
                         vk::Format shadowDepthFormat);

    void setCullResources(vk::DescriptorSetLayout cullSetLayout, std::vector<vk::ImageView> hiZViews, vk::Sampler hiZSampler,
                          std::vector<vk::Buffer> cameraUniformBuffers);
    void updateHiZViews(const std::vector<vk::ImageView> &hiZViews, vk::Sampler hiZSampler);

    RenderObjectHandle addObject(RenderObject object);
    void removeObject(RenderObjectHandle handle);
    [[nodiscard]] RenderObject &getObject(const RenderObjectHandle handle) { return slots[handle].object; }

    void applyPendingChanges();

    void update(uint32_t currentImage, const CullingUtils::Frustum &frustum);

    struct CullCommand {
        vk::raii::CommandBuffer *commandBuffer;
        vk::Extent2D *extent;
        vk::PipelineLayout pipelineLayout;
        vk::Pipeline pipeline;
        uint32_t frameIndex;
        uint32_t maxMip;
        float time;
        bool occlusionEnabled;
    };
    void cull(const CullCommand &command) const;

    using MapDescriptorSets = MaterialDescriptorSets;

    struct DrawCommand {
        vk::raii::CommandBuffer *commandBuffer;
        vk::PipelineLayout pipelineLayout;
        uint32_t frameIndex;

        MapDescriptorSets *materialDescriptorSets;
    };

    void draw(DrawCommand command, bool wireframe, uint32_t &drawCallCount) const;

    void drawXray(DrawCommand command) const;

    void drawShadow(DrawCommand command) const;

    [[nodiscard]] uint64_t getVisibleVertexEstimate(uint32_t frameIndex) const;

  private:
    struct InstanceData {
        glm::vec3 position;
        float padding0;

        glm::vec3 rotation;
        float padding1;

        glm::vec2 uvScale;
        float padding2;
        float padding3;
    };
    static_assert(sizeof(InstanceData) == 48,
                  "InstanceData layout must match InstanceDataGPU exactly (std430: alignment of array elements)");

    struct InstanceBatch {
        PerFrameBuffer buffers;
        PerFrameBuffer culledBuffers;
        PerFrameBuffer indirectBuffers;

        std::vector<vk::raii::DescriptorSet> cullDescriptorSets;

        PerFrameBuffer culledOnlyBuffers;
        PerFrameBuffer culledOnlyIndirectBuffers;

        PerFrameBuffer shadowBuffers;

        uint32_t instanceCount = 0;
        uint32_t visibleInstanceCount = 0;
        uint32_t shadowInstanceCount = 0;
        float boundingRadius = 0.0f;

        std::shared_ptr<Mesh> mesh;
        std::shared_ptr<Texture> texture;
        std::shared_ptr<Texture> normalMap;
        std::shared_ptr<Texture> ormMap;

        std::vector<RenderObjectHandle> memberHandles;

        bool occupied = false;
        bool pendingDestroy = false;
    };

    struct Slot {
        RenderObject object;
        bool occupied = false;
        size_t batchIndex = 0;
        size_t positionInBatch = 0;
    };

    struct BatchKey {
        const Mesh *mesh = nullptr;
        std::shared_ptr<Texture> albedo;
        std::shared_ptr<Texture> normalMap;
        std::shared_ptr<Texture> ormMap;

        bool operator==(const BatchKey &other) const = default;
    };

    struct BatchKeyHash {
        size_t operator()(const BatchKey &key) const noexcept {
            const size_t h0 = std::hash<const Mesh *>{}(key.mesh);
            const size_t h1 = std::hash<std::shared_ptr<Texture>>{}(key.albedo);
            const size_t h2 = std::hash<std::shared_ptr<Texture>>{}(key.normalMap);
            const size_t h3 = std::hash<std::shared_ptr<Texture>>{}(key.ormMap);
            return ((h0 ^ (h1 << 1)) >> 1) ^ ((h2 << 1) ^ (h3 << 2));
        }
    };

    static constexpr size_t MAX_CONCURRENT_BATCHES = 4096;

    struct RetiredBatchResources {
        PerFrameBuffer buffers;
        PerFrameBuffer culledBuffers;
        PerFrameBuffer indirectBuffers;
        PerFrameBuffer culledOnlyBuffers;
        PerFrameBuffer culledOnlyIndirectBuffers;
        PerFrameBuffer shadowBuffers;
        std::vector<vk::raii::DescriptorSet> cullDescriptorSets;
        int framesRemaining = 0;
    };
    std::vector<RetiredBatchResources> retiringResources;

    void retireBatchResources(InstanceBatch &batch);
    void decrementRetiredResources();

    VulkanContext &vkCtx;
    int maxFramesInFlight;

    vk::raii::Pipeline solidPipeline = nullptr;
    vk::raii::Pipeline wireframePipeline = nullptr;
    vk::raii::Pipeline xrayPipeline = nullptr;
    vk::raii::Pipeline shadowPipeline = nullptr;

    vk::raii::DescriptorPool cullDescriptorPool = nullptr;
    bool cullResourcesReady = false;

    vk::DescriptorSetLayout cachedCullSetLayout;
    std::vector<vk::ImageView> cachedHiZViews;
    vk::Sampler cachedHiZSampler;
    std::vector<vk::Buffer> cachedCameraUniformBuffers;

    std::vector<Slot> slots;
    std::vector<RenderObjectHandle> freeSlots;

    std::vector<InstanceBatch> batches;
    std::vector<size_t> freeBatchSlots;
    std::unordered_map<BatchKey, size_t, BatchKeyHash> batchLookup;
    std::unordered_set<size_t> dirtyBatches;

    void recreateBatchGpuResources(InstanceBatch &batch);
    void createCullDescriptorSetForBatch(InstanceBatch &batch);
};

#endif
