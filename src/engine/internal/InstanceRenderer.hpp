#ifndef INSTANCE_RENDERER_HPP
#define INSTANCE_RENDERER_HPP

#pragma once
#include "CullingUtils.hpp"
#include "Mesh.hpp"
#include "PerFrameBuffer.hpp"
#include "PipelineBuilder.hpp"
#include "RenderObject.hpp"
#include "Texture.hpp"
#include <unordered_map>

class InstanceRenderer {
  public:
    explicit InstanceRenderer(VulkanContext &vkCtx, int maxFramesInFlight);

    void createPipelines(vk::PipelineLayout pipelineLayout, vk::Format colorFormat, vk::Format depthFormat);

    RenderObjectHandle addObject(RenderObject object);
    [[nodiscard]] RenderObject &getObject(const RenderObjectHandle handle) { return objects[handle]; }

    void build();
    void update(uint32_t currentImage, const CullingUtils::Frustum &frustum);

    void createCullDescriptorSets(vk::DescriptorSetLayout cullSetLayout, const std::vector<vk::ImageView> &hiZViews,
                                  vk::Sampler hiZSampler, const std::vector<vk::Buffer> &cameraUniformBuffers);

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

    struct MaterialKey {
        std::shared_ptr<Texture> albedo;
        std::shared_ptr<Texture> normalMap;
        std::shared_ptr<Texture> ormMap;

        bool operator==(const MaterialKey &other) const = default;
    };

    struct MaterialKeyHash {
        size_t operator()(const MaterialKey &key) const noexcept {
            const size_t h1 = std::hash<std::shared_ptr<Texture>>{}(key.albedo);
            const size_t h2 = std::hash<std::shared_ptr<Texture>>{}(key.normalMap);
            const size_t h3 = std::hash<std::shared_ptr<Texture>>{}(key.ormMap);
            return ((h1 ^ (h2 << 1)) >> 1) ^ (h3 << 1);
        }
    };

    using MapDescriptorSets = std::unordered_map<MaterialKey, std::vector<vk::raii::DescriptorSet>, MaterialKeyHash>;

    struct DrawCommand {
        vk::raii::CommandBuffer *commandBuffer;
        vk::PipelineLayout pipelineLayout;
        uint32_t frameIndex;

        MapDescriptorSets *materialDescriptorSets;
    };

    void draw(DrawCommand command, bool wireframe, uint32_t &drawCallCount) const;

    void drawXray(DrawCommand command) const;

    [[nodiscard]] uint64_t getVisibleVertexEstimate(uint32_t frameIndex) const;

    void updateHiZViews(const std::vector<vk::ImageView> &hiZViews, vk::Sampler hiZSampler) const;

  private:
    struct InstanceData {
        glm::vec3 position;
        float pad0 = 0.0f;
        glm::vec3 rotation;
        float pad1 = 0.0f;
    };
    static_assert(sizeof(InstanceData) == 32, "InstanceData layout must match InstanceDataGPU exactly");

    struct InstanceBatch {
        PerFrameBuffer buffers;
        PerFrameBuffer culledBuffers;
        PerFrameBuffer indirectBuffers;

        std::vector<vk::raii::DescriptorSet> cullDescriptorSets;

        PerFrameBuffer culledOnlyBuffers;
        PerFrameBuffer culledOnlyIndirectBuffers;

        uint32_t instanceCount = 0;
        uint32_t visibleInstanceCount = 0;
        float boundingRadius = 0.0f;

        std::shared_ptr<Mesh> mesh;
        std::shared_ptr<Texture> texture;
        std::shared_ptr<Texture> normalMap;
        std::shared_ptr<Texture> ormMap;

        std::vector<size_t> objectIndices;
    };

    VulkanContext &vkCtx;
    int maxFramesInFlight;

    vk::raii::Pipeline solidPipeline = nullptr;
    vk::raii::Pipeline wireframePipeline = nullptr;
    vk::raii::Pipeline xrayPipeline = nullptr;

    vk::raii::DescriptorPool cullDescriptorPool = nullptr;

    std::vector<RenderObject> objects;
    std::vector<InstanceBatch> batches;
};

#endif
