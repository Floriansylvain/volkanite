#include "InstanceRenderer.hpp"
#include "OcclusionCuller.hpp"
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

    vk::VertexInputBindingDescription debugBindingDescription{};
    debugBindingDescription.binding = 2;
    debugBindingDescription.stride = sizeof(float);
    debugBindingDescription.inputRate = vk::VertexInputRate::eInstance;

    vk::VertexInputAttributeDescription culledDescription{};
    culledDescription.location = 5;
    culledDescription.binding = 2;
    culledDescription.format = vk::Format::eR32Sfloat;
    culledDescription.offset = 0;

    const std::array debugBindings{bindingDescription, instanceBindingDescription, debugBindingDescription};
    std::vector debugAttributes(attributeDescriptions.begin(), attributeDescriptions.end());
    debugAttributes.push_back(instancePosDescription);
    debugAttributes.push_back(instanceRotationDescription);
    debugAttributes.push_back(culledDescription);

    vk::PipelineVertexInputStateCreateInfo debugVertexInputInfo;
    debugVertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(debugBindings.size());
    debugVertexInputInfo.pVertexBindingDescriptions = debugBindings.data();
    debugVertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(debugAttributes.size());
    debugVertexInputInfo.pVertexAttributeDescriptions = debugAttributes.data();

    vk::PipelineShaderStageCreateInfo debugVertStage = vertShaderStageInfo;
    debugVertStage.pName = "vertMainInstancedDebug";
    const std::array debugShaderStages{debugVertStage, fragShaderStageInfo};

    basePipelineInfo.pStages = debugShaderStages.data();
    basePipelineInfo.pVertexInputState = &debugVertexInputInfo;
    basePipelineInfo.pRasterizationState = &rasterizer;

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

    vk::PipelineShaderStageCreateInfo xrayFragStage = fragShaderStageInfo;
    xrayFragStage.pName = "fragMainXray";
    const std::array xrayShaderStages{instancedVertStage, xrayFragStage};

    vk::PipelineDepthStencilStateCreateInfo xrayDepthStencil{};
    xrayDepthStencil.depthTestEnable = vk::False;
    xrayDepthStencil.depthWriteEnable = vk::False;

    rasterizer.polygonMode = vk::PolygonMode::eFill;
    basePipelineInfo.pStages = xrayShaderStages.data();
    basePipelineInfo.pVertexInputState = &instancedVertexInputInfo;
    basePipelineInfo.pDepthStencilState = &xrayDepthStencil;
    basePipelineInfo.pRasterizationState = &rasterizer;

    vk::StructureChain xrayChain = {basePipelineInfo, pipelineRenderingCreateInfo};
    xrayPipeline = vk::raii::Pipeline(vkCtx.device, nullptr, xrayChain.get<vk::GraphicsPipelineCreateInfo>());
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
            vk::raii::DescriptorSet set = std::move(vk::raii::DescriptorSets(vkCtx.device, allocInfo).front());

            vk::DescriptorBufferInfo inputInfo{*batch.buffers[f], 0, VK_WHOLE_SIZE};
            vk::DescriptorBufferInfo outputInfo{*batch.culledBuffers[f], 0, VK_WHOLE_SIZE};
            vk::DescriptorBufferInfo indirectInfo{*batch.indirectBuffers[f], 0, VK_WHOLE_SIZE};

            const std::vector<vk::WriteDescriptorSet> writes{
                {*set, 0, 0, 1, eStorageBuffer, nullptr, &inputInfo},
                {*set, 1, 0, 1, eStorageBuffer, nullptr, &outputInfo},
                {*set, 2, 0, 1, eStorageBuffer, nullptr, &indirectInfo},
            };
            vkCtx.device.updateDescriptorSets(writes, {});

            vk::DescriptorImageInfo hiZInfo{};
            hiZInfo.sampler = hiZSampler;
            hiZInfo.imageView = hiZViews[f];
            hiZInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::DescriptorBufferInfo cameraInfo{cameraUniformBuffers[f], 0, VK_WHOLE_SIZE};

            const std::vector<vk::WriteDescriptorSet> extraWrites{
                {*set, 3, 0, 1, eCombinedImageSampler, &hiZInfo},
                {*set, 4, 0, 1, eUniformBuffer, nullptr, &cameraInfo},
            };
            vkCtx.device.updateDescriptorSets(extraWrites, {});

            vk::DescriptorBufferInfo culledOutputInfo{*batch.culledOnlyBuffers[f], 0, VK_WHOLE_SIZE};
            vk::DescriptorBufferInfo culledIndirectInfo{*batch.culledOnlyIndirectBuffers[f], 0, VK_WHOLE_SIZE};

            const std::vector<vk::WriteDescriptorSet> debugWrites{
                {*set, 6, 0, 1, eStorageBuffer, nullptr, &culledOutputInfo},
                {*set, 7, 0, 1, eStorageBuffer, nullptr, &culledIndirectInfo},
            };
            vkCtx.device.updateDescriptorSets(debugWrites, {});

            batch.cullDescriptorSets.push_back(std::move(set));
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
    for (auto &batch : batches) {
        for (uint32_t f = 0; f < static_cast<uint32_t>(maxFramesInFlight); ++f) {
            vk::DescriptorImageInfo hiZInfo{};
            hiZInfo.sampler = hiZSampler;
            hiZInfo.imageView = hiZViews[f];
            hiZInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            const vk::WriteDescriptorSet write{
                *batch.cullDescriptorSets[f], 3, 0, 1, vk::DescriptorType::eCombinedImageSampler, &hiZInfo};
            vkCtx.device.updateDescriptorSets(write, {});
        }
    }
}
