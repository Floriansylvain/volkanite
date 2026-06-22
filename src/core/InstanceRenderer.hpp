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

    size_t addObject(RenderObject object);
    [[nodiscard]] RenderObject &getObject(const size_t index) { return objects[index]; }

    void build(const vk::raii::CommandPool &commandPool);
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

    void draw(const vk::raii::CommandBuffer &commandBuffer, uint32_t frameIndex, vk::PipelineLayout pipelineLayout,
              const std::unordered_map<std::shared_ptr<Texture>, std::vector<vk::raii::DescriptorSet>> &textureDescriptorSets,
              bool wireframe, uint32_t &drawCallCount) const;

    void drawXray(
        const vk::raii::CommandBuffer &commandBuffer, uint32_t frameIndex, vk::PipelineLayout pipelineLayout,
        const std::unordered_map<std::shared_ptr<Texture>, std::vector<vk::raii::DescriptorSet>> &textureDescriptorSets) const;

    [[nodiscard]] uint64_t getVisibleVertexEstimate(uint32_t frameIndex) const;

    void updateHiZViews(const std::vector<vk::ImageView> &hiZViews, vk::Sampler hiZSampler) const;

  private:
    struct InstanceData {
        glm::vec3 position;
        float rotation;
    };

    struct InstanceBatch {
        PerFrameBuffer buffers;
        PerFrameBuffer culledBuffers;
        PerFrameBuffer indirectBuffers;

        std::vector<vk::raii::DescriptorSet> cullDescriptorSets;

        PerFrameBuffer culledOnlyBuffers;
        PerFrameBuffer culledOnlyIndirectBuffers;

        vk::raii::Buffer candidateBuffer = nullptr;
        vk::raii::DeviceMemory candidateBufferMemory = nullptr;

        uint32_t instanceCount = 0;
        uint32_t visibleInstanceCount = 0;
        float boundingRadius = 0.0f;
        std::shared_ptr<Mesh> mesh;
        std::shared_ptr<Texture> texture;
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
