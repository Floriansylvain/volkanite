#include "InstanceRenderer.hpp"
#include "DescriptorWriter.hpp"
#include "OcclusionCuller.hpp"
#include "PerFrameBuffer.hpp"
#include "VulkanUtils.hpp"

InstanceRenderer::InstanceRenderer(VulkanContext &vkCtx, const int maxFramesInFlight)
    : vkCtx(vkCtx), maxFramesInFlight(maxFramesInFlight) {}

void InstanceRenderer::createPipelines(const vk::PipelineLayout pipelineLayout, const vk::Format colorFormat,
                                       const vk::Format depthFormat, const vk::Format shadowDepthFormat) {
    const auto bindingDescription = Mesh::Vertex::getBindingDescription();
    const auto attributeDescriptions = Mesh::Vertex::getAttributeDescriptions();

    const vk::VertexInputBindingDescription instanceBinding{1, sizeof(InstanceData), vk::VertexInputRate::eInstance};
    const vk::VertexInputAttributeDescription instancePos{5, 1, vk::Format::eR32G32B32Sfloat, offsetof(InstanceData, position)};
    const vk::VertexInputAttributeDescription instanceRot{6, 1, vk::Format::eR32G32B32Sfloat, offsetof(InstanceData, rotation)};
    const vk::VertexInputAttributeDescription instanceUVScale{7, 1, vk::Format::eR32G32Sfloat, offsetof(InstanceData, uvScale)};

    std::vector attrs(attributeDescriptions.begin(), attributeDescriptions.end());
    attrs.push_back(instancePos);
    attrs.push_back(instanceRot);
    attrs.push_back(instanceUVScale);

    GraphicsPipelineBuilder builder(vkCtx);
    builder.addShaderStage(vk::ShaderStageFlagBits::eVertex, "shaders/shader.spv", "vertMainInstanced")
        .addShaderStage(vk::ShaderStageFlagBits::eFragment, "shaders/shader.spv", "fragMain")
        .setVertexInput({bindingDescription, instanceBinding}, attrs)
        .setLayout(pipelineLayout)
        .setColorFormats({colorFormat})
        .setDepthFormat(depthFormat)
        .setMSAA(vkCtx.msaaSamples);

    solidPipeline = builder.build();

    builder.setPolygonMode(vk::PolygonMode::eLine);
    wireframePipeline = builder.build();

    builder.setPolygonMode(vk::PolygonMode::eFill)
        .setDepthTest(false, false)
        .overrideEntryPoint(vk::ShaderStageFlagBits::eFragment, "fragMainXray");
    xrayPipeline = builder.build();

    const std::vector<vk::VertexInputAttributeDescription> shadowAttrs = {
        attributeDescriptions[0], attributeDescriptions[2], instancePos, instanceRot, instanceUVScale,
    };

    GraphicsPipelineBuilder shadowBuilder(vkCtx);
    shadowBuilder.addShaderStage(vk::ShaderStageFlagBits::eVertex, "shaders/shader.spv", "vertShadowInstanced")
        .addShaderStage(vk::ShaderStageFlagBits::eFragment, "shaders/shader.spv", "fragShadow")
        .setVertexInput({bindingDescription, instanceBinding}, shadowAttrs)
        .setLayout(pipelineLayout)
        .setColorFormats({})
        .setDepthFormat(shadowDepthFormat)
        .setDepthBias(1.5f, 1.75f);

    shadowPipeline = shadowBuilder.build();
}

RenderObjectHandle InstanceRenderer::addObject(RenderObject object) {
    objects.push_back(std::move(object));
    return objects.size() - 1;
}

void InstanceRenderer::build() {
    std::vector<InstanceBatch> newBatches;

    for (size_t i = 0; i < objects.size(); i++) {
        const auto &obj = objects[i];
        auto it = std::ranges::find_if(newBatches, [&](const auto &b) {
            return b.mesh == obj.mesh && b.texture == obj.material.albedo && b.normalMap == obj.material.normalMap &&
                   b.ormMap == obj.material.ormMap;
        });
        if (it == newBatches.end()) {
            InstanceBatch batch;
            batch.mesh = obj.mesh;
            batch.texture = obj.material.albedo;
            batch.normalMap = obj.material.normalMap;
            batch.ormMap = obj.material.ormMap;
            batch.objectIndices.push_back(i);
            newBatches.push_back(std::move(batch));
        } else {
            it->objectIndices.push_back(i);
        }
    }

    for (auto &batch : newBatches) {
        batch.instanceCount = static_cast<uint32_t>(batch.objectIndices.size());
        batch.boundingRadius = batch.mesh->boundingRadius;

        using enum vk::BufferUsageFlagBits;
        using enum vk::MemoryPropertyFlagBits;

        const vk::DeviceSize bufferSize = sizeof(InstanceData) * batch.instanceCount;
        constexpr vk::DeviceSize indirectSize = sizeof(vk::DrawIndexedIndirectCommand);
        const auto frames = static_cast<uint32_t>(maxFramesInFlight);

        batch.buffers = PerFrameBuffer(vkCtx, frames, bufferSize, eVertexBuffer | eStorageBuffer, eHostVisible | eHostCoherent);
        batch.culledBuffers = PerFrameBuffer(vkCtx, frames, bufferSize, eVertexBuffer | eStorageBuffer, eDeviceLocal);
        batch.indirectBuffers = PerFrameBuffer(vkCtx, frames, indirectSize, eIndirectBuffer | eStorageBuffer | eTransferDst,
                                               eHostVisible | eHostCoherent);
        batch.culledOnlyBuffers = PerFrameBuffer(vkCtx, frames, bufferSize, eVertexBuffer | eStorageBuffer, eDeviceLocal);
        batch.culledOnlyIndirectBuffers = PerFrameBuffer(
            vkCtx, frames, indirectSize, eIndirectBuffer | eStorageBuffer | eTransferDst, eHostVisible | eHostCoherent);
        batch.shadowBuffers = PerFrameBuffer(vkCtx, frames, bufferSize, eVertexBuffer, eHostVisible | eHostCoherent);

        const vk::DrawIndexedIndirectCommand initialCommand{static_cast<uint32_t>(batch.mesh->indices.size()), 0, 0, 0, 0};
        for (uint32_t j = 0; j < frames; j++) {
            std::memcpy(batch.indirectBuffers.mapped(j), &initialCommand, indirectSize);
            std::memcpy(batch.culledOnlyIndirectBuffers.mapped(j), &initialCommand, indirectSize);
        }
    }

    batches = std::move(newBatches);
}

void InstanceRenderer::createCullDescriptorSets(const vk::DescriptorSetLayout cullSetLayout,
                                                const std::vector<vk::ImageView> &hiZViews, const vk::Sampler hiZSampler,
                                                const std::vector<vk::Buffer> &cameraUniformBuffers) {
    const auto framesU = static_cast<uint32_t>(maxFramesInFlight);
    const auto totalSets = framesU * static_cast<uint32_t>(batches.size());
    if (totalSets == 0)
        return;

    using enum vk::DescriptorType;
    const std::vector<vk::DescriptorPoolSize> poolSizes = {
        {{eStorageBuffer, totalSets * 5}, {eCombinedImageSampler, totalSets * 1}, {eUniformBuffer, totalSets * 1}}};

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = totalSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes.data();

    cullDescriptorPool = vk::raii::DescriptorPool(vkCtx.device, poolInfo);

    DescriptorWriter writer(vkCtx);

    for (auto &batch : batches) {
        batch.cullDescriptorSets.clear();
        for (uint32_t f = 0; f < framesU; ++f) {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = *cullDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &cullSetLayout;
            vk::raii::DescriptorSet set = std::move(vk::raii::DescriptorSets(vkCtx.device, allocInfo).front());

            writer.writeBuffer(*set, 0, batch.buffers[f], vk::WholeSize, vk::DescriptorType::eStorageBuffer)
                .writeBuffer(*set, 1, batch.culledBuffers[f], vk::WholeSize, vk::DescriptorType::eStorageBuffer)
                .writeBuffer(*set, 2, batch.indirectBuffers[f], vk::WholeSize, vk::DescriptorType::eStorageBuffer)
                .writeImage(*set, 3, vk::DescriptorType::eCombinedImageSampler, hiZViews[f], hiZSampler)
                .writeBuffer(*set, 4, cameraUniformBuffers[f], vk::WholeSize, vk::DescriptorType::eUniformBuffer)
                .writeBuffer(*set, 6, batch.culledOnlyBuffers[f], vk::WholeSize, vk::DescriptorType::eStorageBuffer)
                .writeBuffer(*set, 7, batch.culledOnlyIndirectBuffers[f], vk::WholeSize, vk::DescriptorType::eStorageBuffer);

            batch.cullDescriptorSets.push_back(std::move(set));
        }
    }

    writer.update();
}

void InstanceRenderer::cull(const CullCommand &command) const {
    command.commandBuffer->bindPipeline(vk::PipelineBindPoint::eCompute, command.pipeline);

    for (const auto &batch : batches) {
        command.commandBuffer->fillBuffer(batch.indirectBuffers[command.frameIndex],
                                          offsetof(vk::DrawIndexedIndirectCommand, instanceCount), sizeof(uint32_t), 0);
        command.commandBuffer->fillBuffer(batch.culledOnlyIndirectBuffers[command.frameIndex],
                                          offsetof(vk::DrawIndexedIndirectCommand, instanceCount), sizeof(uint32_t), 0);

        VulkanUtils::BufferBarrierCommand fillBarrier{};
        fillBarrier.buffer = batch.indirectBuffers[command.frameIndex];
        fillBarrier.src_stage_mask = vk::PipelineStageFlagBits2::eClear;
        fillBarrier.src_access_mask = vk::AccessFlagBits2::eTransferWrite;
        fillBarrier.dst_stage_mask = vk::PipelineStageFlagBits2::eComputeShader;
        fillBarrier.dst_access_mask = vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eShaderStorageRead;

        VulkanUtils::BufferBarrierCommand culledOnlyFillBarrier = fillBarrier;
        culledOnlyFillBarrier.buffer = batch.culledOnlyIndirectBuffers[command.frameIndex];

        VulkanUtils::bufferBarriers(*command.commandBuffer, {fillBarrier, culledOnlyFillBarrier});

        if (batch.visibleInstanceCount == 0)
            continue;

        command.commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eCompute, command.pipelineLayout, 2,
                                                  *batch.cullDescriptorSets[command.frameIndex], nullptr);

        OcclusionCuller::PyramidPushConstants pc{};
        pc.srcSize = glm::ivec2(0, 0);
        pc.dstSize = glm::ivec2(command.extent->width, command.extent->height);
        pc.instanceCount = batch.visibleInstanceCount;
        pc.time = command.time;
        pc.boundingRadius = batch.boundingRadius;
        pc.maxMip = command.maxMip;
        pc.boundingCenterX = batch.mesh->boundingCenter.x;
        pc.boundingCenterY = batch.mesh->boundingCenter.y;
        pc.boundingCenterZ = batch.mesh->boundingCenter.z;
        pc.occlusionEnabled = command.occlusionEnabled ? 1u : 0u;

        command.commandBuffer->pushConstants<OcclusionCuller::PyramidPushConstants>(command.pipelineLayout,
                                                                                    vk::ShaderStageFlagBits::eCompute, 0, pc);

        const uint32_t groupCount = (batch.visibleInstanceCount + 63) / 64;
        command.commandBuffer->dispatch(groupCount, 1, 1);

        VulkanUtils::BufferBarrierCommand indirectReadyBarrier{};
        indirectReadyBarrier.buffer = batch.indirectBuffers[command.frameIndex];
        indirectReadyBarrier.src_stage_mask = vk::PipelineStageFlagBits2::eComputeShader;
        indirectReadyBarrier.src_access_mask = vk::AccessFlagBits2::eShaderStorageWrite;
        indirectReadyBarrier.dst_stage_mask = vk::PipelineStageFlagBits2::eDrawIndirect;
        indirectReadyBarrier.dst_access_mask = vk::AccessFlagBits2::eIndirectCommandRead;

        VulkanUtils::BufferBarrierCommand instanceReadyBarrier{};
        instanceReadyBarrier.buffer = batch.culledBuffers[command.frameIndex];
        instanceReadyBarrier.src_stage_mask = vk::PipelineStageFlagBits2::eComputeShader;
        instanceReadyBarrier.src_access_mask = vk::AccessFlagBits2::eShaderStorageWrite;
        instanceReadyBarrier.dst_stage_mask = vk::PipelineStageFlagBits2::eVertexAttributeInput;
        instanceReadyBarrier.dst_access_mask = vk::AccessFlagBits2::eVertexAttributeRead;

        VulkanUtils::BufferBarrierCommand culledOnlyIndirectReadyBarrier = indirectReadyBarrier;
        culledOnlyIndirectReadyBarrier.buffer = batch.culledOnlyIndirectBuffers[command.frameIndex];

        VulkanUtils::BufferBarrierCommand culledOnlyInstanceReadyBarrier = instanceReadyBarrier;
        culledOnlyInstanceReadyBarrier.buffer = batch.culledOnlyBuffers[command.frameIndex];

        VulkanUtils::bufferBarriers(*command.commandBuffer, {indirectReadyBarrier, instanceReadyBarrier,
                                                             culledOnlyIndirectReadyBarrier, culledOnlyInstanceReadyBarrier});
    }
}

void InstanceRenderer::update(const uint32_t currentImage, const CullingUtils::Frustum &frustum) {
    for (auto &batch : batches) {
        auto *dst = static_cast<InstanceData *>(batch.buffers.mapped(currentImage));
        uint32_t visibleCount = 0;

        for (const size_t i : batch.objectIndices) {
            const auto &obj = objects[i];
            if (!CullingUtils::sphereInFrustum(frustum, obj.position + batch.mesh->boundingCenter, batch.boundingRadius))
                continue;

            dst[visibleCount].position = obj.position;
            dst[visibleCount].rotation = obj.rotation;
            dst[visibleCount].uvScale = batch.mesh->uvScale;
            visibleCount++;
        }

        batch.visibleInstanceCount = visibleCount;

        auto *shadowDst = static_cast<InstanceData *>(batch.shadowBuffers.mapped(currentImage));
        for (size_t idx = 0; idx < batch.objectIndices.size(); ++idx) {
            const auto &obj = objects[batch.objectIndices[idx]];
            shadowDst[idx].position = obj.position;
            shadowDst[idx].rotation = obj.rotation;
            shadowDst[idx].uvScale = batch.mesh->uvScale;
        }
    }
}

void InstanceRenderer::draw(DrawCommand command, bool wireframe, uint32_t &drawCallCount) const {
    command.commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, wireframe ? *wireframePipeline : *solidPipeline);

    for (const auto &batch : batches) {
        const MaterialKey key{batch.texture, batch.normalMap, batch.ormMap};
        const auto &materialSets = command.materialDescriptorSets->at(key);
        command.commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, command.pipelineLayout, 0,
                                                  *materialSets[command.frameIndex], nullptr);

        std::vector<vk::DeviceSize> vertexOffsets = {0};
        command.commandBuffer->bindVertexBuffers(0, *batch.mesh->unifiedBuffer, vertexOffsets);
        std::vector<vk::DeviceSize> instanceOffsets = {0};
        command.commandBuffer->bindVertexBuffers(1, batch.culledBuffers[command.frameIndex], instanceOffsets);
        const vk::DeviceSize vertexSizeOffset = sizeof(Mesh::Vertex) * batch.mesh->vertices.size();
        command.commandBuffer->bindIndexBuffer(*batch.mesh->unifiedBuffer, vertexSizeOffset, vk::IndexType::eUint32);

        command.commandBuffer->drawIndexedIndirect(batch.indirectBuffers[command.frameIndex], 0, 1,
                                                   sizeof(vk::DrawIndexedIndirectCommand));
        drawCallCount++;
    }
}

void InstanceRenderer::drawXray(DrawCommand command) const {
    command.commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *xrayPipeline);

    for (const auto &batch : batches) {
        if (batch.visibleInstanceCount == 0)
            continue;

        const MaterialKey key{batch.texture, batch.normalMap, batch.ormMap};
        const auto &materialSets = command.materialDescriptorSets->at(key);
        command.commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, command.pipelineLayout, 0,
                                                  *materialSets[command.frameIndex], nullptr);

        std::vector<vk::DeviceSize> vertexOffsets = {0};
        command.commandBuffer->bindVertexBuffers(0, *batch.mesh->unifiedBuffer, vertexOffsets);
        std::vector<vk::DeviceSize> instanceOffsets = {0};
        command.commandBuffer->bindVertexBuffers(1, batch.culledOnlyBuffers[command.frameIndex], instanceOffsets);
        const vk::DeviceSize vertexSizeOffset = sizeof(Mesh::Vertex) * batch.mesh->vertices.size();
        command.commandBuffer->bindIndexBuffer(*batch.mesh->unifiedBuffer, vertexSizeOffset, vk::IndexType::eUint32);

        command.commandBuffer->drawIndexedIndirect(batch.culledOnlyIndirectBuffers[command.frameIndex], 0, 1,
                                                   sizeof(vk::DrawIndexedIndirectCommand));
    }
}

void InstanceRenderer::drawShadow(DrawCommand command) const {
    command.commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *shadowPipeline);

    for (const auto &batch : batches) {
        if (batch.instanceCount == 0)
            continue;

        const MaterialKey key{batch.texture, batch.normalMap, batch.ormMap};
        const auto &materialSets = command.materialDescriptorSets->at(key);
        command.commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, command.pipelineLayout, 0,
                                                  *materialSets[command.frameIndex], nullptr);

        std::vector<vk::DeviceSize> vertexOffsets = {0};
        command.commandBuffer->bindVertexBuffers(0, *batch.mesh->unifiedBuffer, vertexOffsets);
        std::vector<vk::DeviceSize> instanceOffsets = {0};
        command.commandBuffer->bindVertexBuffers(1, batch.shadowBuffers[command.frameIndex], instanceOffsets);
        const vk::DeviceSize vertexSizeOffset = sizeof(Mesh::Vertex) * batch.mesh->vertices.size();
        command.commandBuffer->bindIndexBuffer(*batch.mesh->unifiedBuffer, vertexSizeOffset, vk::IndexType::eUint32);

        command.commandBuffer->drawIndexed(static_cast<uint32_t>(batch.mesh->indices.size()), batch.instanceCount, 0, 0, 0);
    }
}

uint64_t InstanceRenderer::getVisibleVertexEstimate(const uint32_t frameIndex) const {
    uint64_t total = 0;
    for (const auto &batch : batches) {
        const auto *cmd = static_cast<const vk::DrawIndexedIndirectCommand *>(batch.indirectBuffers.mapped(frameIndex));
        total += static_cast<uint64_t>(batch.mesh->indices.size()) * cmd->instanceCount;
    }
    return total;
}

void InstanceRenderer::updateHiZViews(const std::vector<vk::ImageView> &hiZViews, const vk::Sampler hiZSampler) const {
    DescriptorWriter writer(vkCtx);
    for (auto &batch : batches) {
        for (uint32_t f = 0; f < static_cast<uint32_t>(maxFramesInFlight); ++f) {
            writer.writeImage(*batch.cullDescriptorSets[f], 3, vk::DescriptorType::eCombinedImageSampler, hiZViews[f],
                              hiZSampler);
        }
    }
    writer.update();
}
