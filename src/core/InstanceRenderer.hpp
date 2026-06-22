#ifndef INSTANCE_RENDERER_HPP
#define INSTANCE_RENDERER_HPP

#pragma once
#include "CullingUtils.hpp"
#include "Mesh.hpp"
#include "RenderObject.hpp"
#include "Texture.hpp"
#include "VulkanContext.hpp"
#include <glm/glm.hpp>
#include <unordered_map>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

class InstanceRenderer {
  public:
    explicit InstanceRenderer(VulkanContext &vkCtx, int maxFramesInFlight);

    void createPipelines(const vk::PipelineShaderStageCreateInfo &vertShaderStageInfo,
                         const vk::PipelineShaderStageCreateInfo &fragShaderStageInfo,
                         vk::GraphicsPipelineCreateInfo basePipelineInfo,
                         const vk::PipelineRenderingCreateInfo &pipelineRenderingCreateInfo,
                         vk::PipelineRasterizationStateCreateInfo rasterizer);

    size_t addObject(RenderObject object);
    [[nodiscard]] RenderObject &getObject(const size_t index) { return objects[index]; }

    void build(const vk::raii::CommandPool &commandPool);
    void update(uint32_t currentImage, const CullingUtils::Frustum &frustum);

    void createCullDescriptorSets(vk::DescriptorSetLayout cullSetLayout, const std::vector<vk::ImageView> &hiZViews,
                                  vk::Sampler hiZSampler, const std::vector<vk::Buffer> &cameraUniformBuffers);

    void cull(const vk::raii::CommandBuffer &commandBuffer, uint32_t frameIndex, vk::PipelineLayout pipelineLayout,
              vk::Pipeline pipeline, float time, uint32_t maxMip, const vk::Extent2D &extent, bool occlusionEnabled);

    void draw(const vk::raii::CommandBuffer &commandBuffer, uint32_t frameIndex, vk::PipelineLayout pipelineLayout,
              const std::unordered_map<std::shared_ptr<Texture>, std::vector<vk::raii::DescriptorSet>> &textureDescriptorSets,
              bool wireframe, uint32_t &drawCallCount, uint64_t &vertexCount) const;

    void drawXray(
        const vk::raii::CommandBuffer &commandBuffer, uint32_t frameIndex, vk::PipelineLayout pipelineLayout,
        const std::unordered_map<std::shared_ptr<Texture>, std::vector<vk::raii::DescriptorSet>> &textureDescriptorSets) const;

    [[nodiscard]] uint64_t getVisibleVertexEstimate(uint32_t frameIndex) const;

    void updateHiZViews(const std::vector<vk::ImageView> &hiZViews, vk::Sampler hiZSampler);

  private:
    struct InstanceData {
        glm::vec3 position;
        float rotation;
    };

    struct InstanceBatch {
        std::vector<vk::raii::Buffer> buffers;
        std::vector<vk::raii::DeviceMemory> buffersMemory;
        std::vector<void *> buffersMapped;

        std::vector<vk::raii::Buffer> culledBuffers;
        std::vector<vk::raii::DeviceMemory> culledBuffersMemory;

        std::vector<vk::raii::Buffer> indirectBuffers;
        std::vector<vk::raii::DeviceMemory> indirectBuffersMemory;
        std::vector<void *> indirectBuffersMapped;

        std::vector<vk::raii::DescriptorSet> cullDescriptorSets;

        std::vector<vk::raii::Buffer> culledOnlyBuffers;
        std::vector<vk::raii::DeviceMemory> culledOnlyBuffersMemory;

        std::vector<vk::raii::Buffer> culledOnlyIndirectBuffers;
        std::vector<vk::raii::DeviceMemory> culledOnlyIndirectBuffersMemory;
        std::vector<void *> culledOnlyIndirectBuffersMapped;

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
