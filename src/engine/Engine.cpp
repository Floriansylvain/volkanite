#include "Engine.hpp"
#include "CullingUtils.hpp"
#include "DescriptorWriter.hpp"
#include "Exceptions.hpp"
#include "MeshUtils.hpp"
#include "TerrainSystem.hpp"
#include "VulkanUtils.hpp"
#include "Window.hpp"
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <ranges>

Engine::Engine(Window *_window, VulkanContext *_vkCtx)
    : window(*_window), vkCtx(*_vkCtx), swapChainHandler(vkCtx, window), instanceRenderer(vkCtx, MAX_FRAMES_IN_FLIGHT),
      textRenderer(vkCtx, MAX_FRAMES_IN_FLIGHT), occlusionCuller(vkCtx, MAX_FRAMES_IN_FLIGHT),
      terrainPatchRenderer(vkCtx, MAX_FRAMES_IN_FLIGHT) {}

Engine::~Engine() = default;

void Engine::createGraphicsPipeline() {
    pipelineLayout = VulkanUtils::createPipelineLayout(vkCtx, {*descriptorSetLayout});
    const vk::Format colorFormat = swapChainHandler.surfaceFormat.format;
    const vk::Format depthFormat = swapChainHandler.findDepthFormat();

    textRenderer.createPipeline(colorFormat, depthFormat);
    instanceRenderer.createPipelines(*pipelineLayout, colorFormat, depthFormat, shadowDepthFormat);
    terrainPatchRenderer.createPipelines(colorFormat, depthFormat, shadowDepthFormat);
}

void Engine::createCommandPool() {
    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = vkCtx.queueIndex;

    commandPool = vk::raii::CommandPool(vkCtx.device, poolInfo);
}

void Engine::createCommandBuffers() {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    commandBuffers = vk::raii::CommandBuffers(vkCtx.device, allocInfo);
}

void Engine::createQueryPools() {
    timestampPeriodNs = vkCtx.physicalDevice.getProperties().limits.timestampPeriod;

    vk::QueryPoolCreateInfo poolInfo{};
    poolInfo.queryType = vk::QueryType::eTimestamp;
    poolInfo.queryCount = GPU_QUERY_COUNT;

    gpuQueryPools.clear();
    gpuQueryPools.reserve(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        gpuQueryPools.emplace_back(vkCtx.device, poolInfo);
        gpuQueryPools.back().reset(0, GPU_QUERY_COUNT);
    }
}

void Engine::writeTimestamp(const GpuPass pass, const bool isStart, const vk::PipelineStageFlagBits2 stage) const {
    const uint32_t query = static_cast<uint32_t>(pass) * 2 + (isStart ? 0u : 1u);
    commandBuffers[frameIndex].writeTimestamp2(stage, *gpuQueryPools[frameIndex], query);
}

void Engine::collectGpuTimings(const uint32_t slot) {
    std::vector<uint64_t> timestamps;
    vk::Result result;

    std::tie(result, timestamps) = gpuQueryPools[slot].getResults<uint64_t>(
        0, GPU_QUERY_COUNT, GPU_QUERY_COUNT * sizeof(uint64_t), sizeof(uint64_t), vk::QueryResultFlagBits::e64);

    if (result != vk::Result::eSuccess) {
        gpuQueryPools[slot].reset(0, GPU_QUERY_COUNT);
        return;
    }

    if (!debug.lines().empty()) {
        for (size_t i = 0; i < static_cast<size_t>(GpuPass::Count); ++i) {
            const uint64_t startTick = timestamps[i * 2];
            const uint64_t endTick = timestamps[i * 2 + 1];
            const float ms = static_cast<float>(endTick - startTick) * timestampPeriodNs / 1'000'000.0f;
            debug.recordGpuPass(static_cast<GpuPass>(i), ms);
        }
        debug.endGpuSample();
    }

    gpuQueryPools[slot].reset(0, GPU_QUERY_COUNT);
}

void Engine::recordCommandBuffer(const uint32_t imageIndex) {
    using enum vk::ImageLayout;
    using enum vk::PipelineStageFlagBits2;

    drawCallCount = 0;
    vertexCount = 0;

    commandBuffers[frameIndex].begin({});

    writeTimestamp(GpuPass::Total, true, eTopOfPipe);

    const float time = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - engineStartTime).count();

    VulkanUtils::ImageBarrierCommand colorTransition{};
    colorTransition.image = swapChainHandler.images[imageIndex];
    colorTransition.old_layout = eUndefined;
    colorTransition.new_layout = eColorAttachmentOptimal;
    colorTransition.src_access_mask = {};
    colorTransition.dst_access_mask = vk::AccessFlagBits2::eColorAttachmentWrite;
    colorTransition.src_stage_mask = eColorAttachmentOutput;
    colorTransition.dst_stage_mask = eColorAttachmentOutput;
    colorTransition.image_aspect_flags = vk::ImageAspectFlagBits::eColor;

    VulkanUtils::ImageBarrierCommand msaaColorTransition{};
    msaaColorTransition.image = *swapChainHandler.colorImage;
    msaaColorTransition.old_layout = eUndefined;
    msaaColorTransition.new_layout = eColorAttachmentOptimal;
    msaaColorTransition.src_access_mask = {};
    msaaColorTransition.dst_access_mask = vk::AccessFlagBits2::eColorAttachmentWrite;
    msaaColorTransition.src_stage_mask = eColorAttachmentOutput;
    msaaColorTransition.dst_stage_mask = eColorAttachmentOutput;
    msaaColorTransition.image_aspect_flags = vk::ImageAspectFlagBits::eColor;

    VulkanUtils::ImageBarrierCommand depthTransition{};
    depthTransition.image = *swapChainHandler.depthImage;
    depthTransition.old_layout = eUndefined;
    depthTransition.new_layout = eDepthAttachmentOptimal;
    depthTransition.src_access_mask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    depthTransition.dst_access_mask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    depthTransition.src_stage_mask = eEarlyFragmentTests | eLateFragmentTests;
    depthTransition.dst_stage_mask = eEarlyFragmentTests | eLateFragmentTests;
    depthTransition.image_aspect_flags = vk::ImageAspectFlagBits::eDepth;

    writeTimestamp(GpuPass::FrameSetup, true, eTopOfPipe);
    VulkanUtils::imageBarriers(commandBuffers[frameIndex], {colorTransition, msaaColorTransition, depthTransition});
    writeTimestamp(GpuPass::FrameSetup, false, eAllCommands);

    InstanceRenderer::DrawCommand drawCommand = {};
    drawCommand.commandBuffer = &commandBuffers[frameIndex];
    drawCommand.frameIndex = frameIndex;
    drawCommand.pipelineLayout = *pipelineLayout;
    drawCommand.materialDescriptorSets = &materialDescriptorSets;

    VulkanUtils::ImageBarrierCommand shadowToAttachment{};
    shadowToAttachment.image = *shadowDepthImage;
    shadowToAttachment.old_layout = eUndefined;
    shadowToAttachment.new_layout = eDepthAttachmentOptimal;
    shadowToAttachment.src_access_mask = {};
    shadowToAttachment.dst_access_mask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    shadowToAttachment.src_stage_mask = eEarlyFragmentTests | eLateFragmentTests;
    shadowToAttachment.dst_stage_mask = eEarlyFragmentTests | eLateFragmentTests;
    shadowToAttachment.image_aspect_flags = vk::ImageAspectFlagBits::eDepth;
    VulkanUtils::imageBarriers(commandBuffers[frameIndex], {shadowToAttachment});

    constexpr vk::ClearValue shadowClearDepth = vk::ClearDepthStencilValue(1.0f, 0);
    vk::RenderingAttachmentInfo shadowAttachmentInfo = {};
    shadowAttachmentInfo.imageView = *shadowDepthImageView;
    shadowAttachmentInfo.imageLayout = eDepthAttachmentOptimal;
    shadowAttachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
    shadowAttachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
    shadowAttachmentInfo.clearValue = shadowClearDepth;

    vk::Rect2D shadowRenderArea = {};
    shadowRenderArea.offset = vk::Offset2D{0, 0};
    shadowRenderArea.extent = vk::Extent2D{SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};

    vk::RenderingInfo shadowRenderingInfo = {};
    shadowRenderingInfo.renderArea = shadowRenderArea;
    shadowRenderingInfo.layerCount = 1;
    shadowRenderingInfo.colorAttachmentCount = 0;
    shadowRenderingInfo.pDepthAttachment = &shadowAttachmentInfo;

    // TODO: add shadow-map entry to debug and write timings
    commandBuffers[frameIndex].beginRendering(shadowRenderingInfo);
    commandBuffers[frameIndex].setViewport(
        0, vk::Viewport(0.0f, 0.0f, static_cast<float>(SHADOW_MAP_SIZE), static_cast<float>(SHADOW_MAP_SIZE), 0.0f, 1.0f));
    commandBuffers[frameIndex].setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), vk::Extent2D{SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}));
    instanceRenderer.drawShadow(drawCommand);
    // if (terrainSystem) {
    //     terrainPatchRenderer.drawShadow({&commandBuffers[frameIndex], frameIndex});
    // }
    commandBuffers[frameIndex].endRendering();

    VulkanUtils::ImageBarrierCommand shadowToRead{};
    shadowToRead.image = *shadowDepthImage;
    shadowToRead.old_layout = eDepthAttachmentOptimal;
    shadowToRead.new_layout = eShaderReadOnlyOptimal;
    shadowToRead.src_access_mask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    shadowToRead.dst_access_mask = vk::AccessFlagBits2::eShaderRead;
    shadowToRead.src_stage_mask = eLateFragmentTests;
    shadowToRead.dst_stage_mask = eFragmentShader;
    shadowToRead.image_aspect_flags = vk::ImageAspectFlagBits::eDepth;
    VulkanUtils::imageBarriers(commandBuffers[frameIndex], {shadowToRead});

    writeTimestamp(GpuPass::HiZBuild, true, eComputeShader);
    occlusionCuller.prepareDepthResolveTarget(commandBuffers[frameIndex], frameIndex);
    occlusionCuller.buildPyramid(commandBuffers[frameIndex], frameIndex);
    writeTimestamp(GpuPass::HiZBuild, false, eComputeShader);

    InstanceRenderer::CullCommand cullCommand = {};
    cullCommand.commandBuffer = &commandBuffers[frameIndex];
    cullCommand.extent = &swapChainHandler.extent2D;
    cullCommand.pipelineLayout = occlusionCuller.cullPipelineLayout;
    cullCommand.pipeline = occlusionCuller.cullPipeline;
    cullCommand.frameIndex = frameIndex;
    cullCommand.maxMip = occlusionCuller.mipLevels - 1;
    cullCommand.time = time;
    cullCommand.occlusionEnabled = isOcclusionCulled;

    writeTimestamp(GpuPass::Culling, true, eComputeShader);
    instanceRenderer.cull(cullCommand);
    writeTimestamp(GpuPass::Culling, false, eComputeShader);

    constexpr vk::ClearValue clearColor = vk::ClearColorValue(0.35f, 0.38f, 0.42f, 1.0f);
    vk::RenderingAttachmentInfo attachmentInfo = {};
    attachmentInfo.imageView = swapChainHandler.colorImageView;
    attachmentInfo.imageLayout = eColorAttachmentOptimal;
    attachmentInfo.resolveMode = vk::ResolveModeFlagBits::eAverage;
    attachmentInfo.resolveImageView = swapChainHandler.imageViews[imageIndex];
    attachmentInfo.resolveImageLayout = eColorAttachmentOptimal;
    attachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
    attachmentInfo.storeOp = vk::AttachmentStoreOp::eDontCare;
    attachmentInfo.clearValue = clearColor;

    constexpr vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);
    vk::RenderingAttachmentInfo depthAttachmentInfo = {};
    depthAttachmentInfo.imageView = swapChainHandler.depthImageView;
    depthAttachmentInfo.imageLayout = eDepthAttachmentOptimal;
    depthAttachmentInfo.resolveMode = vk::ResolveModeFlagBits::eMax;
    depthAttachmentInfo.resolveImageView = occlusionCuller.resolvedDepthView(frameIndex);
    depthAttachmentInfo.resolveImageLayout = eDepthAttachmentOptimal;
    depthAttachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
    depthAttachmentInfo.storeOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachmentInfo.clearValue = clearDepth;

    vk::Rect2D renderArea = {};
    renderArea.offset = vk::Offset2D{0, 0};
    renderArea.extent = swapChainHandler.extent2D;

    vk::RenderingInfo renderingInfo = {};
    renderingInfo.renderArea = renderArea;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &attachmentInfo;
    renderingInfo.pDepthAttachment = &depthAttachmentInfo;

    commandBuffers[frameIndex].beginRendering(renderingInfo);

    commandBuffers[frameIndex].setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(swapChainHandler.extent2D.width),
                                                           static_cast<float>(swapChainHandler.extent2D.height), 0.0f, 1.0f));
    commandBuffers[frameIndex].setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainHandler.extent2D));

    writeTimestamp(GpuPass::OpaqueGeometry, true, eColorAttachmentOutput);
    instanceRenderer.draw(drawCommand, isWireframe, drawCallCount);
    vertexCount = instanceRenderer.getVisibleVertexEstimate(frameIndex);
    if (terrainSystem) {
        terrainPatchRenderer.draw({&commandBuffers[frameIndex], frameIndex}, isWireframe, drawCallCount);
        vertexCount += terrainPatchRenderer.getVisibleVertexEstimate();
    }
    writeTimestamp(GpuPass::OpaqueGeometry, false, eColorAttachmentOutput);

    writeTimestamp(GpuPass::Xray, true, eColorAttachmentOutput);
    if (isXray) {
        instanceRenderer.drawXray(drawCommand);
    }
    writeTimestamp(GpuPass::Xray, false, eColorAttachmentOutput);

    writeTimestamp(GpuPass::TextOverlay, true, eColorAttachmentOutput);
    float offset = 10.f;
    const auto &debugLines = debug.lines();
    for (size_t i = 0; i < debugLines.size(); i++) {
        textRenderer.drawText(debugLines[i], 10.0f, static_cast<float>(i) + offset, DEBUG_FONT_SIZE,
                              glm::vec3(1.0f, 1.0f, 1.0f), swapChainHandler.extent2D);
        offset += DEBUG_FONT_SIZE - 10.f;
    }

    if (isPerfOverlayVisible) {
        float perfOffset = 10.f;
        const auto &perfDebugLines = debug.perfLines();
        for (size_t i = 0; i < perfDebugLines.size(); i++) {
            textRenderer.drawText(perfDebugLines[i], PERF_PANEL_LEFT_MARGIN, static_cast<float>(i) + perfOffset,
                                  DEBUG_FONT_SIZE, glm::vec3(1.0f, 1.0f, 1.0f), swapChainHandler.extent2D);
            perfOffset += DEBUG_FONT_SIZE - 10.f;
        }
    }

    textRenderer.render(commandBuffers[frameIndex], frameIndex);
    writeTimestamp(GpuPass::TextOverlay, false, eColorAttachmentOutput);

    commandBuffers[frameIndex].endRendering();

    VulkanUtils::ImageBarrierCommand presentTransition{};
    presentTransition.image = swapChainHandler.images[imageIndex];
    presentTransition.old_layout = eColorAttachmentOptimal;
    presentTransition.new_layout = ePresentSrcKHR;
    presentTransition.src_access_mask = vk::AccessFlagBits2::eColorAttachmentWrite;
    presentTransition.dst_access_mask = {};
    presentTransition.src_stage_mask = eColorAttachmentOutput;
    presentTransition.dst_stage_mask = eBottomOfPipe;
    presentTransition.image_aspect_flags = vk::ImageAspectFlagBits::eColor;

    VulkanUtils::imageBarriers(commandBuffers[frameIndex], {presentTransition});

    writeTimestamp(GpuPass::Total, false, eBottomOfPipe);

    commandBuffers[frameIndex].end();
}

void Engine::createSyncObjects() {
    assert(presentCompleteSemaphores.empty() && renderFinishedSemaphores.empty() && inFlightFences.empty());

    std::ranges::for_each(swapChainHandler.images, [&](const auto &) {
        renderFinishedSemaphores.emplace_back(vkCtx.device, vk::SemaphoreCreateInfo());
    });

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        presentCompleteSemaphores.emplace_back(vkCtx.device, vk::SemaphoreCreateInfo());

        vk::FenceCreateInfo fenceInfo{};
        fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

        inFlightFences.emplace_back(vkCtx.device, fenceInfo);
    }
}

void Engine::recreateSwapChain() {
    swapChainHandler.recreate();
    occlusionCuller.createResources(swapChainHandler.extent2D, swapChainHandler.findDepthFormat());

    std::vector<vk::ImageView> hiZViews;
    hiZViews.reserve(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f) {
        hiZViews.push_back(*occlusionCuller.hiZFullViews[f]);
    }
    instanceRenderer.updateHiZViews(hiZViews, *occlusionCuller.hiZSampler);
}

Material Engine::finalizeMaterial(Material material) {
    if (!material.normalMap) {
        material.normalMap = defaultNormalMap;
    }
    if (!material.ormMap) {
        material.ormMap = getOrCreateFlatOrmMap(material.roughness, material.metallic);
    }
    return material;
}

RenderObjectHandle Engine::addRenderObject(RenderObject object) {
    object.material = finalizeMaterial(std::move(object.material));
    registerMaterial(object.material);

    return instanceRenderer.addObject(std::move(object));
}

void Engine::removeRenderObject(const RenderObjectHandle handle) { instanceRenderer.removeObject(handle); }

RenderObject &Engine::getRenderObject(const RenderObjectHandle handle) { return instanceRenderer.getObject(handle); }

void Engine::drawFrame() {
    const auto waitStart = std::chrono::high_resolution_clock::now();

    if (const auto res =
            vkCtx.device.waitForFences(*inFlightFences[frameIndex], vk::True, std::numeric_limits<uint64_t>::max());
        res == vk::Result::eErrorDeviceLost) {
        throw EngineExceptions::Render("Wait for fences: Device was lost");
    }
    debug.addCpuTime(CpuPass::FenceWait, waitStart);

    collectGpuTimings(frameIndex);

    const auto acquireStart = std::chrono::high_resolution_clock::now();

    uint32_t imageIndex;
    vk::Result result;

    try {
        std::tie(result, imageIndex) =
            swapChainHandler.swapChainKHR.acquireNextImage(UINT64_MAX, *presentCompleteSemaphores[frameIndex], nullptr);
    } catch (const vk::OutOfDateKHRError &) {
        recreateSwapChain();
        return;
    }
    debug.addCpuTime(CpuPass::AcquireImage, acquireStart);
    if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
        assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
        throw EngineExceptions::Render("Failed to acquire swap chain image");
    }

    const auto bufferUpdateStart = std::chrono::high_resolution_clock::now();
    vkCtx.device.resetFences(*inFlightFences[frameIndex]);

    commandBuffers[frameIndex].reset();

    updateUniformBuffer(frameIndex);
    updateInstanceBuffers(frameIndex);
    debug.addCpuTime(CpuPass::BufferUpdate, bufferUpdateStart);

    const auto recordStart = std::chrono::high_resolution_clock::now();
    recordCommandBuffer(imageIndex);
    debug.addCpuTime(CpuPass::RecordCmd, recordStart);

    constexpr vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    vk::SubmitInfo submitInfo{};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &*presentCompleteSemaphores[frameIndex];
    submitInfo.pWaitDstStageMask = &waitDestinationStageMask;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &*commandBuffers[frameIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &*renderFinishedSemaphores[imageIndex];

    const auto submitStart = std::chrono::high_resolution_clock::now();
    vkCtx.queue.submit(submitInfo, *inFlightFences[frameIndex]);
    debug.addCpuTime(CpuPass::Submit, submitStart);

    vk::PresentInfoKHR presentInfoKHR{};
    presentInfoKHR.waitSemaphoreCount = 1;
    presentInfoKHR.pWaitSemaphores = &*renderFinishedSemaphores[imageIndex];
    presentInfoKHR.swapchainCount = 1;
    presentInfoKHR.pSwapchains = &*swapChainHandler.swapChainKHR;
    presentInfoKHR.pImageIndices = &imageIndex;

    const auto presentStart = std::chrono::high_resolution_clock::now();
    try {
        result = vkCtx.queue.presentKHR(presentInfoKHR);
    } catch (const vk::OutOfDateKHRError &) {
        debug.addCpuTime(CpuPass::Present, presentStart);
        framebufferResized = false;
        recreateSwapChain();
        frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
        return;
    }
    debug.addCpuTime(CpuPass::Present, presentStart);

    if (result == vk::Result::eSuboptimalKHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapChain();
    } else {
        assert(result == vk::Result::eSuccess);
    }
    frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

vk::VertexInputBindingDescription Mesh::Vertex::getBindingDescription() {
    vk::VertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = vk::VertexInputRate::eVertex;

    return bindingDescription;
}

std::array<vk::VertexInputAttributeDescription, 5> Mesh::Vertex::getAttributeDescriptions() {
    vk::VertexInputAttributeDescription positionDescription = {};
    positionDescription.location = 0;
    positionDescription.binding = 0;
    positionDescription.format = vk::Format::eR32G32B32Sfloat;
    positionDescription.offset = offsetof(Vertex, pos);

    vk::VertexInputAttributeDescription colorDescription = {};
    colorDescription.location = 1;
    colorDescription.binding = 0;
    colorDescription.format = vk::Format::eR32G32B32Sfloat;
    colorDescription.offset = offsetof(Vertex, color);

    vk::VertexInputAttributeDescription texCoordDescription = {};
    texCoordDescription.location = 2;
    texCoordDescription.binding = 0;
    texCoordDescription.format = vk::Format::eR32G32Sfloat;
    texCoordDescription.offset = offsetof(Vertex, texCoord);

    vk::VertexInputAttributeDescription normalDescription = {};
    normalDescription.location = 3;
    normalDescription.binding = 0;
    normalDescription.format = vk::Format::eR32G32B32Sfloat;
    normalDescription.offset = offsetof(Vertex, normal);

    vk::VertexInputAttributeDescription tangentDescription = {};
    tangentDescription.location = 4;
    tangentDescription.binding = 0;
    tangentDescription.format = vk::Format::eR32G32B32Sfloat;
    tangentDescription.offset = offsetof(Vertex, tangent);

    return {positionDescription, colorDescription, texCoordDescription, normalDescription, tangentDescription};
}

void Engine::createDescriptorSetLayout() {
    using enum vk::ShaderStageFlagBits;
    using enum vk::DescriptorType;

    vk::DescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = eUniformBuffer;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = eVertex | eFragment;

    vk::DescriptorSetLayoutBinding albedoBinding{};
    albedoBinding.binding = 1;
    albedoBinding.descriptorType = eCombinedImageSampler;
    albedoBinding.descriptorCount = 1;
    albedoBinding.stageFlags = eFragment;

    vk::DescriptorSetLayoutBinding normalMapBinding{};
    normalMapBinding.binding = 2;
    normalMapBinding.descriptorType = eCombinedImageSampler;
    normalMapBinding.descriptorCount = 1;
    normalMapBinding.stageFlags = eFragment;

    vk::DescriptorSetLayoutBinding ormMapBinding{};
    ormMapBinding.binding = 3;
    ormMapBinding.descriptorType = eCombinedImageSampler;
    ormMapBinding.descriptorCount = 1;
    ormMapBinding.stageFlags = eFragment;

    vk::DescriptorSetLayoutBinding shadowMapBinding{};
    shadowMapBinding.binding = 4;
    shadowMapBinding.descriptorType = eCombinedImageSampler;
    shadowMapBinding.descriptorCount = 1;
    shadowMapBinding.stageFlags = eFragment;

    descriptorSetLayout = VulkanUtils::createDescriptorSetLayout(
        vkCtx, {uboLayoutBinding, albedoBinding, normalMapBinding, ormMapBinding, shadowMapBinding});
}

void Engine::createCameraUniformBuffer() {
    cameraUniformBuffers = PerFrameBuffer(vkCtx, static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT), sizeof(UniformBufferObject),
                                          vk::BufferUsageFlagBits::eUniformBuffer,
                                          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
}

void Engine::createShadowResources() {
    using enum vk::ImageUsageFlagBits;
    VulkanUtils::CreateImageCommand createImageCommand = {};
    createImageCommand.width = SHADOW_MAP_SIZE;
    createImageCommand.height = SHADOW_MAP_SIZE;
    createImageCommand.mipLevels = 1;
    createImageCommand.samples = vk::SampleCountFlagBits::e1;
    createImageCommand.format = shadowDepthFormat;
    createImageCommand.tiling = vk::ImageTiling::eOptimal;
    createImageCommand.usage = eDepthStencilAttachment | eSampled;
    createImageCommand.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

    std::tie(shadowDepthImage, shadowDepthImageMemory) = VulkanUtils::createImage(vkCtx, createImageCommand);
    shadowDepthImageView =
        VulkanUtils::createImageView(vkCtx, shadowDepthImage, shadowDepthFormat, vk::ImageAspectFlagBits::eDepth, 1);

    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToBorder;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToBorder;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToBorder;
    samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
    samplerInfo.anisotropyEnable = vk::False;
    samplerInfo.compareEnable = vk::False;
    samplerInfo.compareOp = vk::CompareOp::eAlways;

    shadowSampler = vk::raii::Sampler(vkCtx.device, samplerInfo);
}

glm::mat4 Engine::computeLightViewProj() const {
    return Camera::lightViewProjMatrix(lightDirection, camera.position(), SHADOW_LIGHT_DISTANCE, SHADOW_ORTHO_HALF_EXTENT, 0.1f,
                                       SHADOW_LIGHT_DISTANCE * 2.0f);
}

void Engine::updateUniformBuffer(const uint32_t currentImage) const {
    const float aspect = swapChainHandler.getAspectRatio();

    UniformBufferObject ubo{};
    ubo.view = camera.viewMatrix();
    ubo.proj = Camera::projMatrix(aspect);
    ubo.cameraPos = glm::vec4(camera.position(), 1.0f);
    ubo.lightViewProj = computeLightViewProj();
    ubo.lightDir = glm::vec4(lightDirection, 1.0f / static_cast<float>(SHADOW_MAP_SIZE));
    std::memcpy(cameraUniformBuffers.mapped(currentImage), &ubo, sizeof(ubo));
}

void Engine::createDescriptorPool() {
    constexpr vk::DescriptorPoolSize uboPoolSize{vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT * MAX_TEXTURES};
    constexpr vk::DescriptorPoolSize samplerPoolSize{vk::DescriptorType::eCombinedImageSampler,
                                                     MAX_FRAMES_IN_FLIGHT * MAX_TEXTURES * 4};
    constexpr std::array poolSize = {uboPoolSize, samplerPoolSize};

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT * MAX_TEXTURES;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSize.size());
    poolInfo.pPoolSizes = poolSize.data();

    descriptorPool = vk::raii::DescriptorPool(vkCtx.device, poolInfo);
}

void Engine::registerMaterial(const Material &material) {
    const MaterialKey key{material.albedo, material.normalMap, material.ormMap};
    if (materialDescriptorSets.contains(key))
        return;

    const std::vector layouts(MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = *descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();

    std::vector<vk::raii::DescriptorSet> sets = vkCtx.device.allocateDescriptorSets(allocInfo);

    DescriptorWriter writer(vkCtx);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        using enum vk::DescriptorType;

        writer.writeBuffer(*sets[i], 0, cameraUniformBuffers[i], sizeof(UniformBufferObject), eUniformBuffer)
            .writeImage(*sets[i], 1, eCombinedImageSampler, *material.albedo->textureImageView,
                        *material.albedo->textureSampler)
            .writeImage(*sets[i], 2, eCombinedImageSampler, *material.normalMap->textureImageView,
                        *material.normalMap->textureSampler)
            .writeImage(*sets[i], 3, eCombinedImageSampler, *material.ormMap->textureImageView,
                        *material.ormMap->textureSampler)
            .writeImage(*sets[i], 4, eCombinedImageSampler, *shadowDepthImageView, *shadowSampler);
    }
    writer.update();

    materialDescriptorSets.try_emplace(key, std::move(sets));
}

std::shared_ptr<Texture> Engine::getOrCreateFlatOrmMap(const float roughness, const float metallic) {
    const auto key = std::make_pair(roughness, metallic);
    if (const auto it = ormFallbackCache.find(key); it != ormFallbackCache.end()) {
        return it->second;
    }

    auto texture = std::make_shared<Texture>(vkCtx);
    texture->createSolidValue3(glm::vec3(roughness, metallic, 1.0f), commandPool);
    ormFallbackCache.try_emplace(key, texture);
    return texture;
}

std::shared_ptr<Texture> Engine::loadLinearTexture(const std::string &path) const {
    auto texture = std::make_shared<Texture>(vkCtx);
    texture->loadFromFile(path, commandPool, false);
    return texture;
}

std::shared_ptr<Texture> Engine::loadNormalMap(const std::string &path) const { return loadLinearTexture(path); }

std::shared_ptr<Texture> Engine::loadOrmMapFile(const std::string &path) const { return loadLinearTexture(path); }

std::shared_ptr<Texture> Engine::loadOrmMap(const std::string &roughnessPath, const std::string &metallicPath,
                                            const std::string &heightPath) const {
    auto texture = std::make_shared<Texture>(vkCtx);
    texture->loadPackedChannels(roughnessPath, 0.5f, metallicPath, 0.0f, heightPath, 1.0f, commandPool);
    return texture;
}

std::shared_ptr<Mesh> Engine::createCubeMesh(const float size) const {
    auto mesh = std::make_shared<Mesh>(vkCtx);
    MeshUtils::generateCube(*mesh, size);
    mesh->createGeometryBuffers(commandPool);
    return mesh;
}

std::shared_ptr<Mesh> Engine::createMeshFromData(std::vector<Mesh::Vertex> vertices, std::vector<uint32_t> indices) const {
    auto mesh = std::make_shared<Mesh>(vkCtx);
    mesh->vertices = std::move(vertices);
    mesh->indices = std::move(indices);
    mesh->createGeometryBuffers(commandPool);
    return mesh;
}

TerrainSystem &Engine::createTerrain(const TerrainConfig &config) {
    terrainPatchRenderer.createGridMeshes(commandPool, config);

    std::vector<TerrainMaterialLayer> finalizedLayers = config.materialLayers;
    for (auto &layer : finalizedLayers) {
        layer.material = finalizeMaterial(layer.material);
    }
    terrainPatchRenderer.setMaterialLayers(finalizedLayers, cameraUniformBuffers.handles(), *shadowDepthImageView,
                                           *shadowSampler);

    terrainSystem = std::make_unique<TerrainSystem>(config);
    terrainSystem->update(camera.position(), computeCullFrustum());
    terrainPatchRenderer.setPatches(terrainSystem->activePatches(), terrainSystem->activeFinePatches());

    return *terrainSystem;
}

std::shared_ptr<Texture> Engine::loadTexture(const std::string &path) const {
    auto texture = std::make_shared<Texture>(vkCtx);
    texture->loadFromFile(path, commandPool);
    return texture;
}

CullingUtils::Frustum Engine::computeCullFrustum() const {
    const float aspect = swapChainHandler.getAspectRatio();
    constexpr float cullFovMargin = 4.0f;
    const glm::mat4 cullProj = Camera::projMatrix(aspect, 55.0f + cullFovMargin);
    return CullingUtils::extractFrustum(cullProj * camera.viewMatrix());
}

void Engine::updateInstanceBuffers(const uint32_t currentImage) {
    const CullingUtils::Frustum frustum = computeCullFrustum();

    instanceRenderer.update(currentImage, frustum);
    terrainPatchRenderer.upload(currentImage);
}

std::string Engine::resolvePath(const std::string &filename, const std::string &extension) {
    if (filename.empty())
        return "";
    return (std::filesystem::path("textures") / filename).replace_extension(extension).string();
}

std::shared_ptr<Texture> Engine::loadTexture(const std::string &filename,
                                             std::unordered_map<std::string, std::shared_ptr<Texture>> &cache,
                                             const std::string &extension, const bool isSrgb) const {
    if (filename.empty())
        return nullptr;
    if (const auto it = cache.find(filename); it != cache.end())
        return it->second;

    auto texture = std::make_shared<Texture>(vkCtx);
    texture->loadFromFile(resolvePath(filename, extension), commandPool, isSrgb);
    cache.try_emplace(filename, texture);
    return texture;
}

std::shared_ptr<Texture> Engine::loadOrmMap(const MergedGroup &group, const std::string &extension) {
    if (group.roughnessMapFilename.empty() && group.metallicMapFilename.empty() && group.heightMapFilename.empty())
        return nullptr;

    const std::string ormKey =
        std::format("{}|{}|{}", group.roughnessMapFilename, group.metallicMapFilename, group.heightMapFilename);
    if (const auto it = ormMapCache.find(ormKey); it != ormMapCache.end())
        return it->second;

    auto texture = std::make_shared<Texture>(vkCtx);
    texture->loadPackedChannels(resolvePath(group.roughnessMapFilename, extension), group.roughness,
                                resolvePath(group.metallicMapFilename, extension), group.metallic,
                                resolvePath(group.heightMapFilename, extension), 1.0f, commandPool);
    ormMapCache.try_emplace(ormKey, texture);
    return texture;
}

Engine::FBXModel Engine::createFBXModel(const std::string &fbxPath, const std::string &fileExtension) {
    const auto subMeshes = MeshUtils::loadFBXModel(fbxPath);
    std::unordered_map<std::string, MergedGroup, StringHash, std::equal_to<>> merged;

    for (const auto &[albedoFilename, normalMapFilename, roughnessMapFilename, metallicMapFilename, heightMapFilename,
                      baseColor, roughness, metallic, vertices, indices] : subMeshes) {
        if (vertices.empty())
            continue;

        const std::string colorKey =
            albedoFilename.empty() ? std::format("#flatColor:{},{},{}", baseColor.r, baseColor.g, baseColor.b) : albedoFilename;

        const std::string roughnessKey =
            roughnessMapFilename.empty() ? std::format("#flatRoughness:{}", roughness) : roughnessMapFilename;

        const std::string metallicKey =
            metallicMapFilename.empty() ? std::format("#flatMetallic:{}", metallic) : metallicMapFilename;

        const std::string materialKey =
            std::format("{}|{}|{}|{}|{}", colorKey, normalMapFilename, roughnessKey, metallicKey, heightMapFilename);

        auto &group = merged[materialKey];
        group.albedoFilename = albedoFilename;
        group.normalMapFilename = normalMapFilename;
        group.roughnessMapFilename = roughnessMapFilename;
        group.metallicMapFilename = metallicMapFilename;
        group.baseColor = baseColor;
        group.roughness = roughness;
        group.metallic = metallic;
        group.heightMapFilename = heightMapFilename;

        const auto vertexOffset = static_cast<uint32_t>(group.vertices.size());
        group.vertices.insert(group.vertices.end(), vertices.begin(), vertices.end());
        group.indices.reserve(group.indices.size() + indices.size());
        for (const uint32_t idx : indices) {
            group.indices.push_back(idx + vertexOffset);
        }
    }

    FBXModel model;
    for (auto &group : merged | std::views::values) {
        MeshUtils::deduplicateVertices(group.vertices, group.indices);
        MeshUtils::computeTangents(group.vertices, group.indices);

        Material material;
        material.baseColor = group.baseColor;
        material.roughness = group.roughness;
        material.metallic = group.metallic;

        if (!group.albedoFilename.empty()) {
            material.albedo = loadTexture(group.albedoFilename, textureCache, fileExtension, true);
        } else {
            material.albedo = std::make_shared<Texture>(vkCtx);
            material.albedo->createSolidColor(group.baseColor, commandPool);
        }

        material.normalMap = loadTexture(group.normalMapFilename, normalMapCache, fileExtension, false);
        material.ormMap = loadOrmMap(group, fileExtension);

        auto mesh = std::make_shared<Mesh>(vkCtx);
        mesh->vertices = std::move(group.vertices);
        mesh->indices = std::move(group.indices);
        mesh->createGeometryBuffers(commandPool);

        model.meshes.push_back(mesh);
        model.materials.push_back(material);
    }

    return model;
}

void Engine::placeFBXModel(const FBXModel &model, const glm::vec3 &position) {
    for (size_t i = 0; i < model.meshes.size(); ++i) {
        RenderObject object;
        object.mesh = model.meshes[i];
        object.material = model.materials[i];
        object.position = position;
        addRenderObject(std::move(object));
    }
}

void Engine::createOcclusionCuller() {
    occlusionCuller.init();
    occlusionCuller.createResources(swapChainHandler.extent2D, swapChainHandler.findDepthFormat());
    occlusionCuller.createPipelines();
    occlusionCuller.createCullPipeline();
}

DebugFrameInfo Engine::makeDebugFrameInfo() const {
    DebugFrameInfo info;
    info.msaaSamples = static_cast<uint32_t>(vkCtx.msaaSamples);
    info.maxAnisotropy = vkCtx.physicalDevice.getProperties().limits.maxSamplerAnisotropy;
    info.vsyncOn = swapChainHandler.presentMode != vk::PresentModeKHR::eImmediate;
    info.presentModeName = vk::to_string(swapChainHandler.presentMode);
    info.drawCallCount = drawCallCount;
    info.vertexCount = vertexCount;
    info.cameraX = camera.x;
    info.cameraY = camera.y;
    info.cameraZ = camera.z;
    info.cameraYaw = camera.yaw;
    info.cameraPitch = camera.pitch;
    info.wireframe = isWireframe;
    info.occlusionCulling = isOcclusionCulled;
    info.xray = isXray;
    info.perfOverlayVisible = isPerfOverlayVisible;
    return info;
}

void Engine::init() {
    if (!window.isRunning()) {
        throw EngineExceptions::NotInitialized("Failed to run Engine : Window is not running");
    }

    engineStartTime = std::chrono::high_resolution_clock::now();

    window.setChangeCallback([this] { framebufferResized = true; });
    window.setWireframeCallback([this] { isWireframe = !isWireframe; });
    window.setOcclusionCullCallback([this] { isOcclusionCulled = !isOcclusionCulled; });
    window.setXrayCallback([this] { isXray = !isXray; });
    window.setPerfOverlayCallback([this] { isPerfOverlayVisible = !isPerfOverlayVisible; });

    vkCtx.init(window);
    swapChainHandler.create();
    swapChainHandler.createImageViews();
    createDescriptorSetLayout();
    textRenderer.createDescriptorSetLayout();
    terrainPatchRenderer.createMaterialSetLayout();
    createGraphicsPipeline();
    createCommandPool();
    createQueryPools();
    swapChainHandler.createColorResources();
    swapChainHandler.createDepthResources();
    createShadowResources();
    createOcclusionCuller();
    createCameraUniformBuffer();
    createDescriptorPool();

    defaultNormalMap = std::make_shared<Texture>(vkCtx);
    defaultNormalMap->createFlatNormalMap(commandPool);

    textRenderer.loadFont("textures/consolas.png", commandPool, 0.38f, 0.2f);

    std::vector<vk::ImageView> hiZViews;
    hiZViews.reserve(occlusionCuller.hiZFullViews.size());
    for (const auto &view : occlusionCuller.hiZFullViews) {
        hiZViews.push_back(*view);
    }

    instanceRenderer.setCullResources(*occlusionCuller.cullSetLayout, hiZViews, *occlusionCuller.hiZSampler,
                                      cameraUniformBuffers.handles());

    if (game) {
        game->init(*this);
    }

    instanceRenderer.applyPendingChanges();

    createCommandBuffers();
    createSyncObjects();

    isInitialized = true;
}

void Engine::update() {
    if (!window.focusToggle)
        return;

    static auto lastTime = std::chrono::high_resolution_clock::now();
    const auto currentTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;
    deltaTime = std::min(deltaTime, 0.1f);

    if (game) {
        game->update(*this, deltaTime);
    }

    if (terrainSystem) {
        terrainSystem->update(camera.position(), computeCullFrustum());
        terrainPatchRenderer.setPatches(terrainSystem->activePatches(), terrainSystem->activeFinePatches());
    }

    instanceRenderer.applyPendingChanges();
}

void Engine::run() {
    if (!isInitialized) {
        throw EngineExceptions::NotInitialized("Failed to run Engine : Engine is not initialized");
    }

    while (window.isRunning()) {
        const auto frameStart = std::chrono::high_resolution_clock::now();

        const auto pollStart = std::chrono::high_resolution_clock::now();
        window.pollEvents();
        debug.addCpuTime(CpuPass::PollEvents, pollStart);

        const auto updateStart = std::chrono::high_resolution_clock::now();
        update();
        debug.addCpuTime(CpuPass::CameraUpdate, updateStart);

        drawFrame();
        debug.update(frameStart, makeDebugFrameInfo());
    }

    vkCtx.device.waitIdle();
}

void Engine::cleanup() { swapChainHandler.cleanup(); }
