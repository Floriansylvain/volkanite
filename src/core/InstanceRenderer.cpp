#include "InstanceRenderer.hpp"
#include "DescriptorWriter.hpp"
#include "OcclusionCuller.hpp"
#include "VulkanUtils.hpp"
#include <array>

InstanceRenderer::InstanceRenderer(VulkanContext &vkCtx, const int maxFramesInFlight)
    : vkCtx(vkCtx), maxFramesInFlight(maxFramesInFlight) {}

void InstanceRenderer::createPipelines(const vk::PipelineLayout pipelineLayout, const vk::Format colorFormat,
                                       const vk::Format depthFormat) {
    const auto bindingDescription = Mesh::Vertex::getBindingDescription();
    const auto attributeDescriptions = Mesh::Vertex::getAttributeDescriptions();

    const vk::VertexInputBindingDescription instanceBinding{1, sizeof(InstanceData), vk::VertexInputRate::eInstance};
    const vk::VertexInputAttributeDescription instancePos{3, 1, vk::Format::eR32G32B32Sfloat, offsetof(InstanceData, position)};
    const vk::VertexInputAttributeDescription instanceRot{4, 1, vk::Format::eR32Sfloat, offsetof(InstanceData, rotation)};

    std::vector attrs(attributeDescriptions.begin(), attributeDescriptions.end());
    attrs.push_back(instancePos);
    attrs.push_back(instanceRot);

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
}

size_t InstanceRenderer::addObject(RenderObject object) {
    objects.push_back(std::move(object));
    return objects.size() - 1;
}

void InstanceRenderer::build(const vk::raii::CommandPool &commandPool) {
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
            constexpr vk::DeviceSize indirectSize = sizeof(vk::DrawIndexedIndirectCommand);
            auto [buffer, memory] = VulkanUtils::createBuffer(
                vkCtx, bufferSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            batch.buffers.push_back(std::move(buffer));
            batch.buffersMemory.push_back(std::move(memory));
            batch.buffersMapped.push_back(batch.buffersMemory[i].mapMemory(0, bufferSize));

            auto [culledBuf, culledMem] = VulkanUtils::createBuffer(
                vkCtx, bufferSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            batch.culledBuffers.push_back(std::move(culledBuf));
            batch.culledBuffersMemory.push_back(std::move(culledMem));

            auto [indirectBuf, indirectMem] =
                VulkanUtils::createBuffer(vkCtx, indirectSize,
                                          vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer |
                                              vk::BufferUsageFlagBits::eTransferDst,
                                          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            batch.indirectBuffers.push_back(std::move(indirectBuf));
            batch.indirectBuffersMemory.push_back(std::move(indirectMem));
            batch.indirectBuffersMapped.push_back(batch.indirectBuffersMemory[i].mapMemory(0, indirectSize));

            auto [culledOnlyBuf, culledOnlyMem] = VulkanUtils::createBuffer(
                vkCtx, bufferSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            batch.culledOnlyBuffers.push_back(std::move(culledOnlyBuf));
            batch.culledOnlyBuffersMemory.push_back(std::move(culledOnlyMem));

            auto [culledOnlyIndirectBuf, culledOnlyIndirectMem] =
                VulkanUtils::createBuffer(vkCtx, indirectSize,
                                          vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer |
                                              vk::BufferUsageFlagBits::eTransferDst,
                                          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            batch.culledOnlyIndirectBuffers.push_back(std::move(culledOnlyIndirectBuf));
            batch.culledOnlyIndirectBuffersMemory.push_back(std::move(culledOnlyIndirectMem));
            batch.culledOnlyIndirectBuffersMapped.push_back(
                batch.culledOnlyIndirectBuffersMemory[i].mapMemory(0, indirectSize));

            const vk::DrawIndexedIndirectCommand initialCommand{static_cast<uint32_t>(batch.mesh->indices.size()), 0, 0, 0, 0};
            std::memcpy(batch.indirectBuffersMapped[i], &initialCommand, indirectSize);
            std::memcpy(batch.culledOnlyIndirectBuffersMapped[i], &initialCommand, indirectSize);
        }

        std::vector<glm::vec4> candidatePositions;
        candidatePositions.reserve(batch.instanceCount);
        for (const size_t idx : batch.objectIndices) {
            candidatePositions.emplace_back(objects[idx].position, objects[idx].rotationSpeed);
        }

        const vk::DeviceSize candidateBufferSize = sizeof(glm::vec4) * candidatePositions.size();
        auto [stagingBuffer, stagingMemory] =
            VulkanUtils::createBuffer(vkCtx, candidateBufferSize, vk::BufferUsageFlagBits::eTransferSrc,
                                      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        void *data = stagingMemory.mapMemory(0, candidateBufferSize);
        std::memcpy(data, candidatePositions.data(), candidateBufferSize);
        stagingMemory.unmapMemory();

        std::tie(batch.candidateBuffer, batch.candidateBufferMemory) = VulkanUtils::createBuffer(
            vkCtx, candidateBufferSize, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        VulkanUtils::copyBuffer(vkCtx, stagingBuffer, batch.candidateBuffer, candidateBufferSize, commandPool);
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

    const std::vector<vk::DescriptorPoolSize> poolSizes = {{{vk::DescriptorType::eStorageBuffer, totalSets * 5},
                                                            {vk::DescriptorType::eCombinedImageSampler, totalSets * 1},
                                                            {vk::DescriptorType::eUniformBuffer, totalSets * 1}}};

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = totalSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes.data();

    cullDescriptorPool = vk::raii::DescriptorPool(vkCtx.device, poolInfo);

    for (auto &batch : batches) {
        batch.cullDescriptorSets.clear();
        for (uint32_t f = 0; f < framesU; ++f) {
            using enum vk::DescriptorType;
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = *cullDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &cullSetLayout;

            DescriptorWriter writer(vkCtx);

            for (auto &batch : batches) {
                batch.cullDescriptorSets.clear();
                for (uint32_t f = 0; f < framesU; ++f) {
                    vk::DescriptorSetAllocateInfo allocInfo{};
                    allocInfo.descriptorPool = *cullDescriptorPool;
                    allocInfo.descriptorSetCount = 1;
                    allocInfo.pSetLayouts = &cullSetLayout;
                    vk::raii::DescriptorSet set = std::move(vk::raii::DescriptorSets(vkCtx.device, allocInfo).front());

                    writer.writeBuffer(*set, 0, *batch.buffers[f], vk::WholeSize, vk::DescriptorType::eStorageBuffer)
                        .writeBuffer(*set, 1, *batch.culledBuffers[f], vk::WholeSize, vk::DescriptorType::eStorageBuffer)
                        .writeBuffer(*set, 2, *batch.indirectBuffers[f], vk::WholeSize, vk::DescriptorType::eStorageBuffer)
                        .writeImage(*set, 3, vk::DescriptorType::eCombinedImageSampler, hiZViews[f], hiZSampler)
                        .writeBuffer(*set, 4, cameraUniformBuffers[f], vk::WholeSize, vk::DescriptorType::eUniformBuffer)
                        .writeBuffer(*set, 6, *batch.culledOnlyBuffers[f], vk::WholeSize, vk::DescriptorType::eStorageBuffer)
                        .writeBuffer(*set, 7, *batch.culledOnlyIndirectBuffers[f], vk::WholeSize,
                                     vk::DescriptorType::eStorageBuffer);

                    batch.cullDescriptorSets.push_back(std::move(set));
                }
            }

            writer.update();
        }
    }
}

void InstanceRenderer::cull(const CullCommand &command) const {
    command.commandBuffer->bindPipeline(vk::PipelineBindPoint::eCompute, command.pipeline);

    for (const auto &batch : batches) {
        command.commandBuffer->fillBuffer(*batch.indirectBuffers[command.frameIndex],
                                          offsetof(vk::DrawIndexedIndirectCommand, instanceCount), sizeof(uint32_t), 0);
        command.commandBuffer->fillBuffer(*batch.culledOnlyIndirectBuffers[command.frameIndex],
                                          offsetof(vk::DrawIndexedIndirectCommand, instanceCount), sizeof(uint32_t), 0);

        std::array<vk::BufferMemoryBarrier2, 2> fillBarriers{};
        fillBarriers[0].srcStageMask = vk::PipelineStageFlagBits2::eClear;
        fillBarriers[0].srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        fillBarriers[0].dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        fillBarriers[0].dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eShaderStorageRead;
        fillBarriers[0].buffer = *batch.indirectBuffers[command.frameIndex];
        fillBarriers[0].size = vk::WholeSize;

        fillBarriers[1] = fillBarriers[0];
        fillBarriers[1].buffer = *batch.culledOnlyIndirectBuffers[command.frameIndex];

        vk::DependencyInfo dependencyInfo{};
        dependencyInfo.bufferMemoryBarrierCount = 2;
        dependencyInfo.pBufferMemoryBarriers = fillBarriers.data();
        command.commandBuffer->pipelineBarrier2(dependencyInfo);

        if (batch.visibleInstanceCount == 0)
            continue;

        vk::BufferMemoryBarrier2 fillBarrier{};
        fillBarrier.srcStageMask = vk::PipelineStageFlagBits2::eClear;
        fillBarrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        fillBarrier.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        fillBarrier.dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eShaderStorageRead;
        fillBarrier.buffer = *batch.indirectBuffers[command.frameIndex];
        fillBarrier.offset = 0;
        fillBarrier.size = vk::WholeSize;

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

        std::array<vk::BufferMemoryBarrier2, 4> postBarriers{};
        postBarriers[0].srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        postBarriers[0].srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
        postBarriers[0].dstStageMask = vk::PipelineStageFlagBits2::eDrawIndirect;
        postBarriers[0].dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead;
        postBarriers[0].buffer = *batch.indirectBuffers[command.frameIndex];
        postBarriers[0].size = vk::WholeSize;

        postBarriers[1].srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        postBarriers[1].srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
        postBarriers[1].dstStageMask = vk::PipelineStageFlagBits2::eVertexAttributeInput;
        postBarriers[1].dstAccessMask = vk::AccessFlagBits2::eVertexAttributeRead;
        postBarriers[1].buffer = *batch.culledBuffers[command.frameIndex];
        postBarriers[1].size = vk::WholeSize;

        postBarriers[2] = postBarriers[0];
        postBarriers[2].buffer = *batch.culledOnlyIndirectBuffers[command.frameIndex];
        postBarriers[3] = postBarriers[1];
        postBarriers[3].buffer = *batch.culledOnlyBuffers[command.frameIndex];

        vk::DependencyInfo postDependencyInfo{};
        postDependencyInfo.bufferMemoryBarrierCount = 4;
        postDependencyInfo.pBufferMemoryBarriers = postBarriers.data();
        command.commandBuffer->pipelineBarrier2(postDependencyInfo);
    }
}

void InstanceRenderer::update(const uint32_t currentImage, const CullingUtils::Frustum &frustum) {
    static auto startTime = std::chrono::high_resolution_clock::now();
    const float time = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();

    for (auto &batch : batches) {
        auto *dst = static_cast<InstanceData *>(batch.buffersMapped[currentImage]);
        uint32_t visibleCount = 0;

        for (const size_t i : batch.objectIndices) {
            const auto &obj = objects[i];
            if (!CullingUtils::sphereInFrustum(frustum, obj.position + batch.mesh->boundingCenter, batch.boundingRadius))
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
    const bool wireframe, uint32_t &drawCallCount) const {

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, wireframe ? *wireframePipeline : *solidPipeline);

    for (const auto &batch : batches) {
        const auto &sets = textureDescriptorSets.at(batch.texture);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, *sets[frameIndex], nullptr);

        std::vector<vk::DeviceSize> vertexOffsets = {0};
        commandBuffer.bindVertexBuffers(0, *batch.mesh->unifiedBuffer, vertexOffsets);
        std::vector<vk::DeviceSize> instanceOffsets = {0};
        commandBuffer.bindVertexBuffers(1, *batch.culledBuffers[frameIndex], instanceOffsets);
        const vk::DeviceSize vertexSizeOffset = sizeof(Mesh::Vertex) * batch.mesh->vertices.size();
        commandBuffer.bindIndexBuffer(*batch.mesh->unifiedBuffer, vertexSizeOffset, vk::IndexType::eUint32);

        commandBuffer.drawIndexedIndirect(*batch.indirectBuffers[frameIndex], 0, 1, sizeof(vk::DrawIndexedIndirectCommand));
        drawCallCount++;
    }
}

void InstanceRenderer::drawXray(
    const vk::raii::CommandBuffer &commandBuffer, const uint32_t frameIndex, const vk::PipelineLayout pipelineLayout,
    const std::unordered_map<std::shared_ptr<Texture>, std::vector<vk::raii::DescriptorSet>> &textureDescriptorSets) const {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *xrayPipeline);

    for (const auto &batch : batches) {
        if (batch.visibleInstanceCount == 0)
            continue;

        const auto &sets = textureDescriptorSets.at(batch.texture);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, *sets[frameIndex], nullptr);

        std::vector<vk::DeviceSize> vertexOffsets = {0};
        commandBuffer.bindVertexBuffers(0, *batch.mesh->unifiedBuffer, vertexOffsets);
        std::vector<vk::DeviceSize> instanceOffsets = {0};
        commandBuffer.bindVertexBuffers(1, *batch.culledOnlyBuffers[frameIndex], instanceOffsets);
        const vk::DeviceSize vertexSizeOffset = sizeof(Mesh::Vertex) * batch.mesh->vertices.size();
        commandBuffer.bindIndexBuffer(*batch.mesh->unifiedBuffer, vertexSizeOffset, vk::IndexType::eUint32);

        commandBuffer.drawIndexedIndirect(*batch.culledOnlyIndirectBuffers[frameIndex], 0, 1,
                                          sizeof(vk::DrawIndexedIndirectCommand));
    }
}

uint64_t InstanceRenderer::getVisibleVertexEstimate(const uint32_t frameIndex) const {
    uint64_t total = 0;
    for (const auto &batch : batches) {
        const auto *cmd = static_cast<const vk::DrawIndexedIndirectCommand *>(batch.indirectBuffersMapped[frameIndex]);
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
