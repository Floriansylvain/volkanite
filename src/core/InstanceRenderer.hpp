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

    void createCullDescriptorSets(vk::DescriptorSetLayout cullSetLayout);
    void cull(const vk::raii::CommandBuffer &commandBuffer, uint32_t frameIndex, vk::PipelineLayout cullLayout,
              vk::Pipeline cullPipeline, float time);

    void draw(const vk::raii::CommandBuffer &commandBuffer, uint32_t frameIndex, vk::PipelineLayout pipelineLayout,
              const std::unordered_map<std::shared_ptr<Texture>, std::vector<vk::raii::DescriptorSet>> &textureDescriptorSets,
              bool wireframe, uint32_t &drawCallCount, uint64_t &vertexCount) const;

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

    vk::raii::DescriptorPool cullDescriptorPool = nullptr;

    std::vector<RenderObject> objects;
    std::vector<InstanceBatch> batches;
};

#endif
