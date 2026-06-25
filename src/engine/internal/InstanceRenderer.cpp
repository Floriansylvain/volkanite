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

    constexpr vk::VertexInputBindingDescription instanceBinding{1, sizeof(InstanceData), vk::VertexInputRate::eInstance};
    constexpr vk::VertexInputAttributeDescription instancePos{5, 1, vk::Format::eR32G32B32Sfloat,
                                                              offsetof(InstanceData, position)};
    constexpr vk::VertexInputAttributeDescription instanceRot{6, 1, vk::Format::eR32G32B32Sfloat,
                                                              offsetof(InstanceData, rotation)};
    constexpr vk::VertexInputAttributeDescription instanceUVScale{7, 1, vk::Format::eR32G32Sfloat,
                                                                  offsetof(InstanceData, uvScale)};

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

void InstanceRenderer::setCullResources(const vk::DescriptorSetLayout cullSetLayout, std::vector<vk::ImageView> hiZViews,
                                        const vk::Sampler hiZSampler, std::vector<vk::Buffer> cameraUniformBuffers) {
    cachedCullSetLayout = cullSetLayout;
    cachedHiZViews = std::move(hiZViews);
    cachedHiZSampler = hiZSampler;
    cachedCameraUniformBuffers = std::move(cameraUniformBuffers);

    const auto framesU = static_cast<uint32_t>(maxFramesInFlight);
    const auto totalSets = framesU * static_cast<uint32_t>(MAX_CONCURRENT_BATCHES);

    using enum vk::DescriptorType;
    const std::array<vk::DescriptorPoolSize, 3> poolSizes = {
        vk::DescriptorPoolSize{eStorageBuffer, totalSets * 5},
        vk::DescriptorPoolSize{eCombinedImageSampler, totalSets * 1},
        vk::DescriptorPoolSize{eUniformBuffer, totalSets * 1},
    };

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = totalSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    cullDescriptorPool = vk::raii::DescriptorPool(vkCtx.device, poolInfo);
    cullResourcesReady = true;
}

void InstanceRenderer::createCullDescriptorSetForBatch(InstanceBatch &batch) const {
    if (!cullResourcesReady) {
        return;
    }

    const auto framesU = static_cast<uint32_t>(maxFramesInFlight);
    batch.cullDescriptorSets.clear();

    DescriptorWriter writer(vkCtx);
    for (uint32_t f = 0; f < framesU; ++f) {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = *cullDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &cachedCullSetLayout;
        vk::raii::DescriptorSet set = std::move(vk::raii::DescriptorSets(vkCtx.device, allocInfo).front());

        using enum vk::DescriptorType;

        writer.writeBuffer(*set, 0, batch.buffers[f], vk::WholeSize, eStorageBuffer)
            .writeBuffer(*set, 1, batch.culledBuffers[f], vk::WholeSize, eStorageBuffer)
            .writeBuffer(*set, 2, batch.indirectBuffers[f], vk::WholeSize, eStorageBuffer)
            .writeImage(*set, 3, eCombinedImageSampler, cachedHiZViews[f], cachedHiZSampler)
            .writeBuffer(*set, 4, cachedCameraUniformBuffers[f], vk::WholeSize, eUniformBuffer)
            .writeBuffer(*set, 6, batch.culledOnlyBuffers[f], vk::WholeSize, eStorageBuffer)
            .writeBuffer(*set, 7, batch.culledOnlyIndirectBuffers[f], vk::WholeSize, eStorageBuffer);

        batch.cullDescriptorSets.push_back(std::move(set));
    }

    writer.update();
}

void InstanceRenderer::recreateBatchGpuResources(InstanceBatch &batch) const {
    using enum vk::BufferUsageFlagBits;
    using enum vk::MemoryPropertyFlagBits;

    batch.instanceCount = static_cast<uint32_t>(batch.memberHandles.size());
    batch.boundingRadius = batch.mesh->boundingRadius;

    const vk::DeviceSize bufferSize = sizeof(InstanceData) * std::max<uint32_t>(batch.instanceCount, 1);
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

    createCullDescriptorSetForBatch(batch);
}

RenderObjectHandle InstanceRenderer::addObject(RenderObject object) {
    RenderObjectHandle handle;
    if (!freeSlots.empty()) {
        handle = freeSlots.back();
        freeSlots.pop_back();
    } else {
        handle = slots.size();
        slots.emplace_back();
    }

    slots[handle].object = std::move(object);
    slots[handle].occupied = true;

    const RenderObject &obj = slots[handle].object;
    const BatchKey key{obj.mesh.get(), obj.material.albedo, obj.material.normalMap, obj.material.ormMap};

    size_t batchIndex;
    if (const auto it = batchLookup.find(key); it != batchLookup.end()) {
        batchIndex = it->second;
    } else {
        if (!freeBatchSlots.empty()) {
            batchIndex = freeBatchSlots.back();
            freeBatchSlots.pop_back();
        } else {
            batchIndex = batches.size();
            batches.emplace_back();
        }

        InstanceBatch &newBatch = batches[batchIndex];
        newBatch = InstanceBatch{};
        newBatch.mesh = obj.mesh;
        newBatch.texture = obj.material.albedo;
        newBatch.normalMap = obj.material.normalMap;
        newBatch.ormMap = obj.material.ormMap;
        newBatch.occupied = true;

        batchLookup.try_emplace(key, batchIndex);
    }

    InstanceBatch &batch = batches[batchIndex];
    slots[handle].batchIndex = batchIndex;
    slots[handle].positionInBatch = batch.memberHandles.size();
    batch.memberHandles.push_back(handle);

    dirtyBatches.insert(batchIndex);

    return handle;
}

void InstanceRenderer::removeObject(const RenderObjectHandle handle) {
    if (handle >= slots.size() || !slots[handle].occupied) {
        return;
    }

    Slot &slot = slots[handle];
    const size_t batchIndex = slot.batchIndex;
    InstanceBatch &batch = batches[batchIndex];

    const size_t pos = slot.positionInBatch;
    if (const size_t lastPos = batch.memberHandles.size() - 1; pos != lastPos) {
        const RenderObjectHandle movedHandle = batch.memberHandles[lastPos];
        batch.memberHandles[pos] = movedHandle;
        slots[movedHandle].positionInBatch = pos;
    }
    batch.memberHandles.pop_back();

    slot.object = RenderObject{};
    slot.occupied = false;
    freeSlots.push_back(handle);

    if (batch.memberHandles.empty()) {
        const BatchKey key{batch.mesh.get(), batch.texture, batch.normalMap, batch.ormMap};
        batchLookup.erase(key);
        batch.pendingDestroy = true;
    }

    dirtyBatches.insert(batchIndex);
}

void InstanceRenderer::retireBatchResources(InstanceBatch &batch) {
    RetiredBatchResources retired;
    retired.buffers = std::move(batch.buffers);
    retired.culledBuffers = std::move(batch.culledBuffers);
    retired.indirectBuffers = std::move(batch.indirectBuffers);
    retired.culledOnlyBuffers = std::move(batch.culledOnlyBuffers);
    retired.culledOnlyIndirectBuffers = std::move(batch.culledOnlyIndirectBuffers);
    retired.shadowBuffers = std::move(batch.shadowBuffers);
    retired.cullDescriptorSets = std::move(batch.cullDescriptorSets);
    retired.framesRemaining = maxFramesInFlight;
    retiringResources.push_back(std::move(retired));
}

void InstanceRenderer::decrementRetiredResources() {
    std::erase_if(retiringResources, [](RetiredBatchResources &retired) { return --retired.framesRemaining <= 0; });
}

void InstanceRenderer::applyPendingChanges() {
    decrementRetiredResources();

    if (dirtyBatches.empty()) {
        return;
    }

    for (const size_t batchIndex : dirtyBatches) {
        InstanceBatch &batch = batches[batchIndex];
        if (batch.pendingDestroy) {
            retireBatchResources(batch);
            batch = InstanceBatch{};
            batch.occupied = false;
            freeBatchSlots.push_back(batchIndex);
        } else if (batch.occupied) {
            retireBatchResources(batch);
            recreateBatchGpuResources(batch);
        }
    }

    dirtyBatches.clear();
}

void InstanceRenderer::update(const uint32_t currentImage, const CullingUtils::Frustum &frustum) {
    for (auto &batch : batches) {
        if (!batch.occupied)
            continue;

        auto *dst = static_cast<InstanceData *>(batch.buffers.mapped(currentImage));
        uint32_t visibleCount = 0;

        for (const RenderObjectHandle handle : batch.memberHandles) {
            const auto &obj = slots[handle].object;
            if (!obj.isVisible)
                continue;
            if (!CullingUtils::sphereInFrustum(frustum, obj.position + batch.mesh->boundingCenter, batch.boundingRadius))
                continue;

            dst[visibleCount].position = obj.position;
            dst[visibleCount].rotation = obj.rotation;
            dst[visibleCount].uvScale = batch.mesh->uvScale;
            visibleCount++;
        }

        batch.visibleInstanceCount = visibleCount;

        auto *shadowDst = static_cast<InstanceData *>(batch.shadowBuffers.mapped(currentImage));
        uint32_t shadowVisibleCount = 0;
        for (const RenderObjectHandle handle : batch.memberHandles) {
            const auto &obj = slots[handle].object;
            if (!obj.isVisible)
                continue;

            shadowDst[shadowVisibleCount].position = obj.position;
            shadowDst[shadowVisibleCount].rotation = obj.rotation;
            shadowDst[shadowVisibleCount].uvScale = batch.mesh->uvScale;
            shadowVisibleCount++;
        }
        batch.shadowInstanceCount = shadowVisibleCount;
    }
}

void InstanceRenderer::draw(const DrawCommand &command, const bool wireframe, uint32_t &drawCallCount) const {
    command.commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, wireframe ? *wireframePipeline : *solidPipeline);

    for (const auto &batch : batches) {
        if (!batch.occupied)
            continue;

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

void InstanceRenderer::drawXray(const DrawCommand &command) const {
    command.commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *xrayPipeline);

    for (const auto &batch : batches) {
        if (!batch.occupied || batch.visibleInstanceCount == 0)
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

void InstanceRenderer::drawShadow(const DrawCommand &command) const {
    command.commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *shadowPipeline);

    for (const auto &batch : batches) {
        if (!batch.occupied || batch.shadowInstanceCount == 0)
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

        command.commandBuffer->drawIndexed(static_cast<uint32_t>(batch.mesh->indices.size()), batch.shadowInstanceCount, 0, 0,
                                           0);
    }
}

uint64_t InstanceRenderer::getVisibleVertexEstimate(const uint32_t frameIndex) const {
    uint64_t total = 0;
    for (const auto &batch : batches) {
        if (!batch.occupied)
            continue;

        const auto *cmd = static_cast<const vk::DrawIndexedIndirectCommand *>(batch.indirectBuffers.mapped(frameIndex));
        total += static_cast<uint64_t>(batch.mesh->indices.size()) * cmd->instanceCount;
    }
    return total;
}

void InstanceRenderer::cull(const CullCommand &command) const {
    command.commandBuffer->bindPipeline(vk::PipelineBindPoint::eCompute, command.pipeline);

    for (const auto &batch : batches) {
        if (!batch.occupied)
            continue;

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

void InstanceRenderer::updateHiZViews(const std::vector<vk::ImageView> &hiZViews, const vk::Sampler hiZSampler) {
    cachedHiZViews = hiZViews;
    cachedHiZSampler = hiZSampler;

    DescriptorWriter writer(vkCtx);
    for (auto &batch : batches) {
        if (!batch.occupied)
            continue;

        for (uint32_t f = 0; f < static_cast<uint32_t>(maxFramesInFlight); ++f) {
            writer.writeImage(*batch.cullDescriptorSets[f], 3, vk::DescriptorType::eCombinedImageSampler, hiZViews[f],
                              hiZSampler);
        }
    }
    writer.update();
}
