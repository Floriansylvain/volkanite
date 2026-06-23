#include "Engine.hpp"
#include "Constants.hpp"
#include "CullingUtils.hpp"
#include "DescriptorWriter.hpp"
#include "Exceptions.hpp"
#include "MeshUtils.hpp"
#include "PipelineBuilder.hpp"
#include "ShaderUtils.hpp"
#include "VulkanUtils.hpp"
#include "Window.hpp"
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>

Engine::Engine(Window *_window, VulkanContext *_vkCtx)
    : window(*_window), vkCtx(*_vkCtx), swapChainHandler(vkCtx, window), instanceRenderer(vkCtx, MAX_FRAMES_IN_FLIGHT),
      textRenderer(vkCtx, MAX_FRAMES_IN_FLIGHT), occlusionCuller(vkCtx, MAX_FRAMES_IN_FLIGHT) {}

Engine::~Engine() = default;

void Engine::createGraphicsPipeline() {
    pipelineLayout = VulkanUtils::createPipelineLayout(vkCtx, {*descriptorSetLayout});

    const vk::Format colorFormat = swapChainHandler.surfaceFormat.format;
    const vk::Format depthFormat = swapChainHandler.findDepthFormat();

    textRenderer.createPipeline(colorFormat, depthFormat);
    instanceRenderer.createPipelines(*pipelineLayout, colorFormat, depthFormat);
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

    constexpr vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
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
    instanceRenderer.draw(commandBuffers[frameIndex], frameIndex, *pipelineLayout, textureDescriptorSets, isWireframe,
                          drawCallCount);
    vertexCount = instanceRenderer.getVisibleVertexEstimate(frameIndex);
    writeTimestamp(GpuPass::OpaqueGeometry, false, eColorAttachmentOutput);

    writeTimestamp(GpuPass::Xray, true, eColorAttachmentOutput);
    if (isXray) {
        instanceRenderer.drawXray(commandBuffers[frameIndex], frameIndex, *pipelineLayout, textureDescriptorSets);
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

    writeTimestamp(GpuPass::Present, true, eColorAttachmentOutput);
    VulkanUtils::imageBarriers(commandBuffers[frameIndex], {presentTransition});
    writeTimestamp(GpuPass::Present, false, eBottomOfPipe);

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

RenderObjectHandle Engine::addRenderObject(RenderObject object) {
    registerTexture(object.texture);
    return instanceRenderer.addObject(std::move(object));
}

RenderObject &Engine::getRenderObject(const RenderObjectHandle handle) { return instanceRenderer.getObject(handle); }

void Engine::drawFrame() {
    const auto waitStart = std::chrono::high_resolution_clock::now();

    const vk::Result waitResult =
        vkCtx.device.waitForFences(*inFlightFences[frameIndex], vk::True, std::numeric_limits<uint64_t>::max());
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

std::array<vk::VertexInputAttributeDescription, 4> Mesh::Vertex::getAttributeDescriptions() {
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

    return {positionDescription, colorDescription, texCoordDescription, normalDescription};
}

void Engine::createDescriptorSetLayout() {
    vk::DescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    descriptorSetLayout = VulkanUtils::createDescriptorSetLayout(vkCtx, {uboLayoutBinding, samplerLayoutBinding});
}

void Engine::createCameraUniformBuffer() {
    cameraUniformBuffers = PerFrameBuffer(vkCtx, static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT), sizeof(UniformBufferObject),
                                          vk::BufferUsageFlagBits::eUniformBuffer,
                                          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
}

void Engine::updateUniformBuffer(const uint32_t currentImage) const {
    const float aspect = swapChainHandler.getAspectRatio();

    UniformBufferObject ubo{};
    ubo.view = camera.viewMatrix();
    ubo.proj = Camera::projMatrix(aspect);
    ubo.cameraPos = glm::vec4(camera.position(), 1.0f);
    std::memcpy(cameraUniformBuffers.mapped(currentImage), &ubo, sizeof(ubo));
}

void Engine::createDescriptorPool() {
    constexpr vk::DescriptorPoolSize uboPoolSize{vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT * MAX_TEXTURES};
    constexpr vk::DescriptorPoolSize samplerPoolSize{vk::DescriptorType::eCombinedImageSampler,
                                                     MAX_FRAMES_IN_FLIGHT * MAX_TEXTURES};
    constexpr std::array poolSize = {uboPoolSize, samplerPoolSize};

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT * MAX_TEXTURES;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSize.size());
    poolInfo.pPoolSizes = poolSize.data();

    descriptorPool = vk::raii::DescriptorPool(vkCtx.device, poolInfo);
}

void Engine::registerTexture(const std::shared_ptr<Texture> &texture) {
    if (textureDescriptorSets.contains(texture))
        return;

    const std::vector layouts(MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = *descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();

    std::vector<vk::raii::DescriptorSet> sets = vkCtx.device.allocateDescriptorSets(allocInfo);

    DescriptorWriter writer(vkCtx);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        writer
            .writeBuffer(*sets[i], 0, cameraUniformBuffers[i], sizeof(UniformBufferObject), vk::DescriptorType::eUniformBuffer)
            .writeImage(*sets[i], 1, vk::DescriptorType::eCombinedImageSampler, *texture->textureImageView,
                        *texture->textureSampler);
    }
    writer.update();

    textureDescriptorSets.try_emplace(texture, std::move(sets));
}

std::shared_ptr<Mesh> Engine::createCubeMesh(const float size) {
    auto mesh = std::make_shared<Mesh>(vkCtx);
    MeshUtils::generateCube(*mesh, size);
    mesh->createGeometryBuffers(commandPool);
    return mesh;
}

std::shared_ptr<Texture> Engine::loadTexture(const std::string &path) {
    auto texture = std::make_shared<Texture>(vkCtx);
    texture->loadFromFile(path, commandPool);
    return texture;
}

void Engine::updateInstanceBuffers(const uint32_t currentImage) {
    const float aspect = swapChainHandler.getAspectRatio();
    constexpr float cullFovMargin = 4.0f;
    const glm::mat4 cullProj = Camera::projMatrix(aspect, 55.0f + cullFovMargin);
    const CullingUtils::Frustum frustum = CullingUtils::extractFrustum(cullProj * camera.viewMatrix());

    instanceRenderer.update(currentImage, frustum);
}

Engine::FBXModel Engine::createFBXModel(const std::string &fbxPath, const std::string &fileExtension) {
    const auto subMeshes = MeshUtils::loadFBXModel(fbxPath);

    struct MergedGroup {
        std::vector<Mesh::Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    std::unordered_map<std::string, MergedGroup, StringHash, std::equal_to<>> merged;

    for (const auto &[filename, vertices, indices] : subMeshes) {
        if (vertices.empty() || filename.empty())
            continue;

        auto &[gVertices, gIndices] = merged[filename];
        const auto vertexOffset = static_cast<uint32_t>(gVertices.size());

        gVertices.insert(gVertices.end(), vertices.begin(), vertices.end());
        gIndices.reserve(gIndices.size() + indices.size());
        for (const uint32_t idx : indices) {
            gIndices.push_back(idx + vertexOffset);
        }
    }

    FBXModel model;

    for (auto &[filename, group] : merged) {
        MeshUtils::deduplicateVertices(group.vertices, group.indices);

        std::shared_ptr<Texture> texture;
        if (const auto it = textureCache.find(filename); it != textureCache.end()) {
            texture = it->second;
        } else {
            std::filesystem::path texPath = std::filesystem::path("textures") / filename;
            texPath.replace_extension(fileExtension);

            texture = std::make_shared<Texture>(vkCtx);
            texture->loadFromFile(texPath.string(), commandPool);
            textureCache.try_emplace(filename, texture);
        }

        auto mesh = std::make_shared<Mesh>(vkCtx);
        mesh->vertices = std::move(group.vertices);
        mesh->indices = std::move(group.indices);
        mesh->createGeometryBuffers(commandPool);

        model.meshes.push_back(mesh);
        model.textures.push_back(texture);
    }

    return model;
}

void Engine::placeFBXModel(const FBXModel &model, const glm::vec3 &position) {
    for (size_t i = 0; i < model.meshes.size(); ++i) {
        RenderObject object;
        object.mesh = model.meshes[i];
        object.texture = model.textures[i];
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
    createGraphicsPipeline();
    createCommandPool();
    createQueryPools();
    swapChainHandler.createColorResources();
    swapChainHandler.createDepthResources();
    createOcclusionCuller();
    createCameraUniformBuffer();
    createDescriptorPool();

    textRenderer.loadFont("textures/consolas.png", commandPool, 0.38f, 0.2f);

    if (game) {
        game->init(*this);
    }

    instanceRenderer.build(commandPool);

    std::vector<vk::ImageView> hiZViews;
    hiZViews.reserve(occlusionCuller.hiZFullViews.size());
    for (const auto &view : occlusionCuller.hiZFullViews) {
        hiZViews.push_back(*view);
    }

    instanceRenderer.createCullDescriptorSets(*occlusionCuller.cullSetLayout, hiZViews, *occlusionCuller.hiZSampler,
                                              cameraUniformBuffers.handles());

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
