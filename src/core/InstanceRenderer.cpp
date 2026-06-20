#include "InstanceRenderer.hpp"
#include "VulkanUtils.hpp"
#include <array>

InstanceRenderer::InstanceRenderer(VulkanContext &vkCtx, const int maxFramesInFlight)
    : vkCtx(vkCtx), maxFramesInFlight(maxFramesInFlight) {}

void InstanceRenderer::createPipelines(const vk::PipelineShaderStageCreateInfo &vertShaderStageInfo,
                                       const vk::PipelineShaderStageCreateInfo &fragShaderStageInfo,
                                       vk::GraphicsPipelineCreateInfo basePipelineInfo,
                                       const vk::PipelineRenderingCreateInfo &pipelineRenderingCreateInfo,
                                       vk::PipelineRasterizationStateCreateInfo rasterizer) {
    const auto bindingDescription = Mesh::Vertex::getBindingDescription();
    const auto attributeDescriptions = Mesh::Vertex::getAttributeDescriptions();

    vk::VertexInputBindingDescription instanceBindingDescription{};
    instanceBindingDescription.binding = 1;
    instanceBindingDescription.stride = sizeof(InstanceData);
    instanceBindingDescription.inputRate = vk::VertexInputRate::eInstance;

    vk::VertexInputAttributeDescription instancePosDescription{};
    instancePosDescription.location = 3;
    instancePosDescription.binding = 1;
    instancePosDescription.format = vk::Format::eR32G32B32Sfloat;
    instancePosDescription.offset = offsetof(InstanceData, position);

    vk::VertexInputAttributeDescription instanceRotationDescription{};
    instanceRotationDescription.location = 4;
    instanceRotationDescription.binding = 1;
    instanceRotationDescription.format = vk::Format::eR32Sfloat;
    instanceRotationDescription.offset = offsetof(InstanceData, rotation);

    const std::array instancedBindings = {bindingDescription, instanceBindingDescription};
    std::vector instancedAttributes(attributeDescriptions.begin(), attributeDescriptions.end());
    instancedAttributes.push_back(instancePosDescription);
    instancedAttributes.push_back(instanceRotationDescription);

    vk::PipelineVertexInputStateCreateInfo instancedVertexInputInfo;
    instancedVertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(instancedBindings.size());
    instancedVertexInputInfo.pVertexBindingDescriptions = instancedBindings.data();
    instancedVertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(instancedAttributes.size());
    instancedVertexInputInfo.pVertexAttributeDescriptions = instancedAttributes.data();

    vk::PipelineShaderStageCreateInfo instancedVertStage = vertShaderStageInfo;
    instancedVertStage.pName = "vertMainInstanced";
    const std::array instancedShaderStages = {instancedVertStage, fragShaderStageInfo};

    basePipelineInfo.pStages = instancedShaderStages.data();
    basePipelineInfo.pVertexInputState = &instancedVertexInputInfo;

    vk::StructureChain solidChain = {basePipelineInfo, pipelineRenderingCreateInfo};
    solidPipeline = vk::raii::Pipeline(vkCtx.device, nullptr, solidChain.get<vk::GraphicsPipelineCreateInfo>());

    rasterizer.polygonMode = vk::PolygonMode::eLine;
    basePipelineInfo.pRasterizationState = &rasterizer;

    vk::StructureChain wireframeChain = {basePipelineInfo, pipelineRenderingCreateInfo};
    wireframePipeline = vk::raii::Pipeline(vkCtx.device, nullptr, wireframeChain.get<vk::GraphicsPipelineCreateInfo>());
}

size_t InstanceRenderer::addObject(RenderObject object) {
    objects.push_back(std::move(object));
    return objects.size() - 1;
}

void InstanceRenderer::build() {
    std::vector<InstanceBatch> newBatches;

    for (size_t i = 0; i < objects.size(); i++) {
        const auto &obj = objects[i];
        auto it =
            std::ranges::find_if(newBatches, [&](const auto &b) { return b.mesh == obj.mesh && b.texture == obj.texture; });
        if (it == newBatches.end()) {
            InstanceBatch batch;
            batch.mesh = obj.mesh;
            batch.texture = obj.texture;
            batch.objectIndices.push_back(i);
            newBatches.push_back(std::move(batch));
        } else {
            it->objectIndices.push_back(i);
        }
    }

    for (auto &batch : newBatches) {
        batch.instanceCount = static_cast<uint32_t>(batch.objectIndices.size());
        batch.boundingRadius = batch.mesh->boundingRadius;
        const vk::DeviceSize bufferSize = sizeof(InstanceData) * batch.instanceCount;

        for (int i = 0; i < maxFramesInFlight; i++) {
            auto [buffer, memory] =
                VulkanUtils::createBuffer(vkCtx, bufferSize, vk::BufferUsageFlagBits::eVertexBuffer,
                                          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            batch.buffers.push_back(std::move(buffer));
            batch.buffersMemory.push_back(std::move(memory));
            batch.buffersMapped.push_back(batch.buffersMemory[i].mapMemory(0, bufferSize));
        }
    }

    batches = std::move(newBatches);
}

void InstanceRenderer::update(const uint32_t currentImage, const CullingUtils::Frustum &frustum) {
    static auto startTime = std::chrono::high_resolution_clock::now();
    const float time = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();

    for (auto &batch : batches) {
        auto *dst = static_cast<InstanceData *>(batch.buffersMapped[currentImage]);
        uint32_t visibleCount = 0;

        for (const size_t i : batch.objectIndices) {
            const auto &obj = objects[i];
            if (!CullingUtils::sphereInFrustum(frustum, obj.position, batch.boundingRadius))
                continue;

            dst[visibleCount].position = obj.position;
            dst[visibleCount].rotation = time * obj.rotationSpeed;
            visibleCount++;
        }

        batch.visibleInstanceCount = visibleCount;
    }
}

void InstanceRenderer::draw(
    const vk::raii::CommandBuffer &commandBuffer, const uint32_t frameIndex, const vk::PipelineLayout pipelineLayout,
    const std::unordered_map<std::shared_ptr<Texture>, std::vector<vk::raii::DescriptorSet>> &textureDescriptorSets,
    const bool wireframe, uint32_t &drawCallCount, uint64_t &vertexCount) const {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, wireframe ? *wireframePipeline : *solidPipeline);

    for (const auto &batch : batches) {
        if (batch.visibleInstanceCount == 0)
            continue;

        const auto &sets = textureDescriptorSets.at(batch.texture);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, *sets[frameIndex], nullptr);

        vk::DeviceSize vertexOffsets[] = {0};
        commandBuffer.bindVertexBuffers(0, *batch.mesh->unifiedBuffer, vertexOffsets);
        vk::DeviceSize instanceOffsets[] = {0};
        commandBuffer.bindVertexBuffers(1, *batch.buffers[frameIndex], instanceOffsets);
        const vk::DeviceSize vertexSizeOffset = sizeof(Mesh::Vertex) * batch.mesh->vertices.size();
        commandBuffer.bindIndexBuffer(*batch.mesh->unifiedBuffer, vertexSizeOffset, vk::IndexType::eUint32);

        commandBuffer.drawIndexed(static_cast<uint32_t>(batch.mesh->indices.size()), batch.visibleInstanceCount, 0, 0, 0);
        drawCallCount++;
        vertexCount += static_cast<uint64_t>(batch.mesh->indices.size()) * batch.visibleInstanceCount;
    }
}