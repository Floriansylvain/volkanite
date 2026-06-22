#include "Engine.hpp"
#include "Constants.hpp"
#include "CullingUtils.hpp"
#include "Exceptions.hpp"
#include "MeshUtils.hpp"
#include "ShaderUtils.hpp"
#include "VulkanUtils.hpp"
#include "Window.hpp"

#include <SDL3_image/SDL_image.h>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>

Engine::Engine(Window *_window, VulkanContext *_vkCtx)
    : window(*_window), vkCtx(*_vkCtx), swapChainHandler(vkCtx, window), instanceRenderer(vkCtx, MAX_FRAMES_IN_FLIGHT),
      textRenderer(vkCtx, MAX_FRAMES_IN_FLIGHT), occlusionCuller(vkCtx, MAX_FRAMES_IN_FLIGHT) {}

Engine::~Engine() = default;

void Engine::createGraphicsPipeline() {
    const vk::raii::ShaderModule shaderModule =
        ShaderUtils::createShaderModule(vkCtx, ShaderUtils::readFile("shaders/shader.spv"));
    const vk::raii::ShaderModule textShaderModule =
        ShaderUtils::createShaderModule(vkCtx, ShaderUtils::readFile("shaders/text.spv"));

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    vertShaderStageInfo.module = shaderModule;
    vertShaderStageInfo.pName = "vertMain";

    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    fragShaderStageInfo.module = shaderModule;
    fragShaderStageInfo.pName = "fragMain";

    std::vector shaderStages = {vertShaderStageInfo, fragShaderStageInfo};

    const std::vector dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.depthClampEnable = vk::False;
    rasterizer.rasterizerDiscardEnable = vk::False;
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.cullMode = vk::CullModeFlagBits::eBack;
    rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
    rasterizer.depthBiasEnable = vk::False;
    rasterizer.lineWidth = 1.0f;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.rasterizationSamples = vkCtx.msaaSamples;
    multisampling.sampleShadingEnable = vk::False;

    using enum vk::ColorComponentFlagBits;
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = vk::True;
    colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachment.colorWriteMask = eR | eG | eB | eA;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = vk::False;
    colorBlending.logicOp = vk::LogicOp::eCopy;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ModelPushConstant);

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &*descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    pipelineLayout = vk::raii::PipelineLayout(vkCtx.device, pipelineLayoutInfo);

    auto bindingDescription = Mesh::Vertex::getBindingDescription();
    auto attributeDescriptions = Mesh::Vertex::getAttributeDescriptions();

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable = vk::True;
    depthStencil.depthWriteEnable = vk::True;
    depthStencil.depthCompareOp = vk::CompareOp::eLess;
    depthStencil.depthBoundsTestEnable = vk::False;
    depthStencil.stencilTestEnable = vk::False;

    vk::GraphicsPipelineCreateInfo graphicsPipelineCreateInfo{};
    graphicsPipelineCreateInfo.stageCount = 2;
    graphicsPipelineCreateInfo.pStages = shaderStages.data();
    graphicsPipelineCreateInfo.pVertexInputState = &vertexInputInfo;
    graphicsPipelineCreateInfo.pInputAssemblyState = &inputAssembly;
    graphicsPipelineCreateInfo.pViewportState = &viewportState;
    graphicsPipelineCreateInfo.pRasterizationState = &rasterizer;
    graphicsPipelineCreateInfo.pMultisampleState = &multisampling;
    graphicsPipelineCreateInfo.pColorBlendState = &colorBlending;
    graphicsPipelineCreateInfo.pDynamicState = &dynamicState;
    graphicsPipelineCreateInfo.layout = pipelineLayout;
    graphicsPipelineCreateInfo.renderPass = nullptr;
    graphicsPipelineCreateInfo.pDepthStencilState = &depthStencil;

    vk::Format depthFormat = swapChainHandler.findDepthFormat();

    vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
    pipelineRenderingCreateInfo.colorAttachmentCount = 1;
    pipelineRenderingCreateInfo.pColorAttachmentFormats = &swapChainHandler.surfaceFormat.format;
    pipelineRenderingCreateInfo.depthAttachmentFormat = depthFormat;

    vk::PipelineShaderStageCreateInfo textVertStageInfo{};
    textVertStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    textVertStageInfo.module = textShaderModule;
    textVertStageInfo.pName = "vertMainText";

    vk::PipelineShaderStageCreateInfo textFragStageInfo{};
    textFragStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    textFragStageInfo.module = textShaderModule;
    textFragStageInfo.pName = "fragMainText";

    textRenderer.createPipeline(textVertStageInfo, textFragStageInfo, pipelineRenderingCreateInfo, vkCtx.msaaSamples);

    vk::StructureChain pipelineCreateInfoChain = {graphicsPipelineCreateInfo, pipelineRenderingCreateInfo};

    solidGraphicsPipeline =
        vk::raii::Pipeline(vkCtx.device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());

    instanceRenderer.createPipelines(vertShaderStageInfo, fragShaderStageInfo, graphicsPipelineCreateInfo,
                                     pipelineRenderingCreateInfo, rasterizer);

    rasterizer.polygonMode = vk::PolygonMode::eLine;

    wireframeGraphicsPipeline =
        vk::raii::Pipeline(vkCtx.device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
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

    if (result != vk::Result::eSuccess)
        return;

    for (size_t i = 0; i < static_cast<size_t>(GpuPass::Count); ++i) {
        const uint64_t startTick = timestamps[i * 2];
        const uint64_t endTick = timestamps[i * 2 + 1];
        const float ms = static_cast<float>(endTick - startTick) * timestampPeriodNs / 1'000'000.0f;
        debug.recordGpuPass(static_cast<GpuPass>(i), ms);
    }
    debug.endGpuSample();
}

void Engine::transition_image_layouts(const std::vector<TransitionImageLayoutCommand> &commands) const {
    std::vector<vk::ImageMemoryBarrier2> barriers;
    barriers.reserve(commands.size());

    for (const auto &[image, old_layout, new_layout, src_access_mask, dst_access_mask, src_stage_mask, dst_stage_mask,
                      image_aspect_flags] : commands) {
        vk::ImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = image_aspect_flags;
        subresourceRange.levelCount = 1;
        subresourceRange.layerCount = 1;

        vk::ImageMemoryBarrier2 barrier{};
        barrier.srcStageMask = src_stage_mask;
        barrier.srcAccessMask = src_access_mask;
        barrier.dstStageMask = dst_stage_mask;
        barrier.dstAccessMask = dst_access_mask;
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = subresourceRange;
        barriers.push_back(barrier);
    }

    vk::DependencyInfo dependencyInfo{};
    dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dependencyInfo.pImageMemoryBarriers = barriers.data();

    commandBuffers[frameIndex].pipelineBarrier2(dependencyInfo);
}

void Engine::buffer_barriers(const std::vector<BufferBarrierCommand> &commands) const {
    std::vector<vk::BufferMemoryBarrier2> barriers;
    barriers.reserve(commands.size());

    for (const auto &command : commands) {
        vk::BufferMemoryBarrier2 barrier{};
        barrier.srcStageMask = command.src_stage_mask;
        barrier.srcAccessMask = command.src_access_mask;
        barrier.dstStageMask = command.dst_stage_mask;
        barrier.dstAccessMask = command.dst_access_mask;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = command.buffer;
        barrier.offset = command.offset;
        barrier.size = command.size;
        barriers.push_back(barrier);
    }

    vk::DependencyInfo dependencyInfo{};
    dependencyInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dependencyInfo.pBufferMemoryBarriers = barriers.data();

    commandBuffers[frameIndex].pipelineBarrier2(dependencyInfo);
}

void Engine::recordCommandBuffer(const uint32_t imageIndex) {
    using enum vk::ImageLayout;
    using enum vk::PipelineStageFlagBits2;

    drawCallCount = 0;
    vertexCount = 0;

    commandBuffers[frameIndex].begin({});

    commandBuffers[frameIndex].resetQueryPool(*gpuQueryPools[frameIndex], 0, GPU_QUERY_COUNT);
    writeTimestamp(GpuPass::Total, true, eTopOfPipe);

    const float time = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - engineStartTime).count();

    TransitionImageLayoutCommand colorTransition{};
    colorTransition.image = swapChainHandler.images[imageIndex];
    colorTransition.old_layout = eUndefined;
    colorTransition.new_layout = eColorAttachmentOptimal;
    colorTransition.src_access_mask = {};
    colorTransition.dst_access_mask = vk::AccessFlagBits2::eColorAttachmentWrite;
    colorTransition.src_stage_mask = eColorAttachmentOutput;
    colorTransition.dst_stage_mask = eColorAttachmentOutput;
    colorTransition.image_aspect_flags = vk::ImageAspectFlagBits::eColor;

    TransitionImageLayoutCommand msaaColorTransition{};
    msaaColorTransition.image = *swapChainHandler.colorImage;
    msaaColorTransition.old_layout = eUndefined;
    msaaColorTransition.new_layout = eColorAttachmentOptimal;
    msaaColorTransition.src_access_mask = {};
    msaaColorTransition.dst_access_mask = vk::AccessFlagBits2::eColorAttachmentWrite;
    msaaColorTransition.src_stage_mask = eColorAttachmentOutput;
    msaaColorTransition.dst_stage_mask = eColorAttachmentOutput;
    msaaColorTransition.image_aspect_flags = vk::ImageAspectFlagBits::eColor;

    TransitionImageLayoutCommand depthTransition{};
    depthTransition.image = *swapChainHandler.depthImage;
    depthTransition.old_layout = eUndefined;
    depthTransition.new_layout = eDepthAttachmentOptimal;
    depthTransition.src_access_mask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    depthTransition.dst_access_mask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    depthTransition.src_stage_mask = eEarlyFragmentTests | eLateFragmentTests;
    depthTransition.dst_stage_mask = eEarlyFragmentTests | eLateFragmentTests;
    depthTransition.image_aspect_flags = vk::ImageAspectFlagBits::eDepth;

    writeTimestamp(GpuPass::FrameSetup, true, eTopOfPipe);
    transition_image_layouts({colorTransition, msaaColorTransition, depthTransition});
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

    if (isWireframe) {
        commandBuffers[frameIndex].bindPipeline(vk::PipelineBindPoint::eGraphics, *wireframeGraphicsPipeline);
    } else {
        commandBuffers[frameIndex].bindPipeline(vk::PipelineBindPoint::eGraphics, *solidGraphicsPipeline);
    }
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

    TransitionImageLayoutCommand presentTransition{};
    presentTransition.image = swapChainHandler.images[imageIndex];
    presentTransition.old_layout = eColorAttachmentOptimal;
    presentTransition.new_layout = ePresentSrcKHR;
    presentTransition.src_access_mask = vk::AccessFlagBits2::eColorAttachmentWrite;
    presentTransition.dst_access_mask = {};
    presentTransition.src_stage_mask = eColorAttachmentOutput;
    presentTransition.dst_stage_mask = eBottomOfPipe;
    presentTransition.image_aspect_flags = vk::ImageAspectFlagBits::eColor;

    writeTimestamp(GpuPass::Present, true, eColorAttachmentOutput);
    transition_image_layouts({presentTransition});
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

void Engine::drawFrame() {
    const auto fenceWaitStart = std::chrono::high_resolution_clock::now();
    if (auto const fenceResult = vkCtx.device.waitForFences(*inFlightFences[frameIndex], vk::True, UINT64_MAX);
        fenceResult != vk::Result::eSuccess) {
        throw EngineExceptions::Render("Failed to wait for fence");
    }
    debug.addCpuTime(CpuPass::FenceWait, fenceWaitStart);

    if (totalFramesRendered >= static_cast<uint64_t>(MAX_FRAMES_IN_FLIGHT)) {
        collectGpuTimings(frameIndex);
    }
    totalFramesRendered++;

    vk::Result result;
    uint32_t imageIndex;

    const auto acquireStart = std::chrono::high_resolution_clock::now();
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

    vk::SubpassDependency dependency{};
    dependency.srcSubpass = vk::SubpassExternal;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.srcAccessMask = vk::AccessFlagBits::eNone;
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

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

std::array<vk::VertexInputAttributeDescription, 3> Mesh::Vertex::getAttributeDescriptions() {
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

    return {positionDescription, colorDescription, texCoordDescription};
}

void Engine::createDescriptorSetLayout() {
    vk::DescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

    vk::DescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    const std::array bindings{uboLayoutBinding, samplerLayoutBinding};

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    descriptorSetLayout = vk::raii::DescriptorSetLayout(vkCtx.device, layoutInfo);
}

void Engine::createCameraUniformBuffer() {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        constexpr vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
        auto [buffer, memory] =
            VulkanUtils::createBuffer(vkCtx, bufferSize, vk::BufferUsageFlagBits::eUniformBuffer,
                                      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

        cameraUniformBuffers.push_back(std::move(buffer));
        cameraUniformBuffersMemory.push_back(std::move(memory));
        cameraUniformBuffersMapped.push_back(cameraUniformBuffersMemory[i].mapMemory(0, bufferSize));
    }
}

void Engine::updateUniformBuffer(const uint32_t currentImage) const {
    const float aspect =
        static_cast<float>(swapChainHandler.extent2D.width) / static_cast<float>(swapChainHandler.extent2D.height);

    UniformBufferObject ubo{};
    ubo.view = camera.viewMatrix();
    ubo.proj = Camera::projMatrix(aspect);
    std::memcpy(cameraUniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
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

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk::DescriptorBufferInfo bufferInfo{*cameraUniformBuffers[i], 0, sizeof(UniformBufferObject)};

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.sampler = *texture->textureSampler;
        imageInfo.imageView = *texture->textureImageView;
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        const vk::WriteDescriptorSet uboWrite{sets[i], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufferInfo};
        const vk::WriteDescriptorSet samplerWrite{sets[i], 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &imageInfo};
        const std::array writes = {uboWrite, samplerWrite};
        vkCtx.device.updateDescriptorSets(writes, {});
    }

    textureDescriptorSets.try_emplace(texture, std::move(sets));
}

void Engine::addRenderObject(RenderObject object) {
    registerTexture(object.texture);
    if (object.isInstanced) {
        instanceRenderer.addObject(std::move(object));
    } else {
        renderObjects.push_back(std::move(object));
    }
}

void Engine::updateInstanceBuffers(const uint32_t currentImage) {
    const float aspect =
        static_cast<float>(swapChainHandler.extent2D.width) / static_cast<float>(swapChainHandler.extent2D.height);
    constexpr float cullFovMargin = 4.0f;
    const glm::mat4 cullProj = Camera::projMatrix(aspect, 55.0f + cullFovMargin);
    const CullingUtils::Frustum frustum = CullingUtils::extractFrustum(cullProj * camera.viewMatrix());

    instanceRenderer.update(currentImage, frustum);

    for (auto &obj : renderObjects) {
        obj.isVisible = CullingUtils::sphereInFrustum(frustum, obj.position, obj.mesh->boundingRadius);
    }
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

void Engine::placeFBXModel(const FBXModel &model, const glm::vec3 &position, const bool instanced) {
    for (size_t i = 0; i < model.meshes.size(); ++i) {
        RenderObject object;
        object.mesh = model.meshes[i];
        object.texture = model.textures[i];
        object.position = position;
        object.isInstanced = instanced;
        object.rotationSpeed = 0.f;
        addRenderObject(std::move(object));
    }
}

void Engine::createOcclusionCuller() {
    occlusionCuller.init();
    occlusionCuller.createResources(swapChainHandler.extent2D, swapChainHandler.findDepthFormat());

    const vk::raii::ShaderModule depthToMip0Module =
        ShaderUtils::createShaderModule(vkCtx, ShaderUtils::readFile("shaders/cull/DepthToMip0.spv"));

    const vk::raii::ShaderModule downsampleModule =
        ShaderUtils::createShaderModule(vkCtx, ShaderUtils::readFile("shaders/cull/Downsample.spv"));

    const vk::raii::ShaderModule cullInstancesModule =
        ShaderUtils::createShaderModule(vkCtx, ShaderUtils::readFile("shaders/cull/CullInstances.spv"));

    vk::PipelineShaderStageCreateInfo depthToMip0StageInfo{};
    depthToMip0StageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    depthToMip0StageInfo.module = *depthToMip0Module;
    depthToMip0StageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo downsampleStageInfo{};
    downsampleStageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    downsampleStageInfo.module = *downsampleModule;
    downsampleStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo cullStageInfo{};
    cullStageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    cullStageInfo.module = *cullInstancesModule;
    cullStageInfo.pName = "main";

    occlusionCuller.createPipelines(depthToMip0StageInfo, downsampleStageInfo);
    occlusionCuller.createCullPipeline(cullStageInfo);
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

    const FBXModel house = createFBXModel("models/House_scene_01.fbx", ".png");
    placeFBXModel(house, glm::vec3(0.f, 0.f, 0.f), true);

    textRenderer.loadFont("textures/consolas.png", commandPool, 0.38f, 0.2f);

    const auto mesh = std::make_shared<Mesh>(vkCtx);
    MeshUtils::generateCube(*mesh, 1.f);
    mesh->createGeometryBuffers(commandPool);

    const auto texture = std::make_shared<Texture>(vkCtx);
    texture->loadFromFile("textures/bricks.jpg", commandPool);

    constexpr int SIZE = 100;
    constexpr int OFFSET = 5;
    for (int x = -SIZE / 2; x < SIZE / 2; x += OFFSET) {
        for (int y = -SIZE / 2; y < SIZE / 2; y += OFFSET) {
            for (int z = -SIZE / 2; z < SIZE / 2; z += OFFSET) {
                RenderObject cube;
                cube.mesh = mesh;
                cube.texture = texture;
                cube.position = {x, y, z};
                cube.isInstanced = true;
                cube.rotationSpeed =
                    glm::sin(static_cast<float>(x)) + glm::sin(static_cast<float>(y)) + glm::sin(static_cast<float>(z));
                addRenderObject(std::move(cube));
            }
        }
    }

    instanceRenderer.build(commandPool);

    std::vector<vk::ImageView> hiZViews;
    hiZViews.reserve(occlusionCuller.hiZFullViews.size());
    for (const auto &view : occlusionCuller.hiZFullViews) {
        hiZViews.push_back(*view);
    }

    std::vector<vk::Buffer> camBuffers;
    camBuffers.reserve(cameraUniformBuffers.size());
    for (const auto &buf : cameraUniformBuffers) {
        camBuffers.push_back(*buf);
    }

    instanceRenderer.createCullDescriptorSets(*occlusionCuller.cullSetLayout, hiZViews, *occlusionCuller.hiZSampler,
                                              camBuffers);

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

    float mouseDx;
    float mouseDy;
    SDL_GetRelativeMouseState(&mouseDx, &mouseDy);

    constexpr float sensitivity = 0.1f;
    camera.yaw -= mouseDx * sensitivity;
    camera.pitch -= mouseDy * sensitivity;

    constexpr float maxPitch = 89.0f;
    camera.pitch = std::clamp(camera.pitch, -maxPitch, maxPitch);

    const float yawRad = glm::radians(camera.yaw);
    const glm::vec3 flatForward(cos(yawRad), sin(yawRad), 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(flatForward, glm::vec3(0.0f, 0.0f, 1.0f)));

    const bool *key_states = SDL_GetKeyboardState(nullptr);
    glm::vec3 input = {0.f, 0.f, 0.f};

    if (key_states[SDL_SCANCODE_W])
        input.y += 1;
    if (key_states[SDL_SCANCODE_S])
        input.y -= 1;
    if (key_states[SDL_SCANCODE_D])
        input.x += 1;
    if (key_states[SDL_SCANCODE_A])
        input.x -= 1;
    if (key_states[SDL_SCANCODE_LCTRL])
        input.z -= 1;
    if (key_states[SDL_SCANCODE_SPACE])
        input.z += 1;

    glm::vec3 movement = flatForward * input.y + right * input.x;
    if (glm::length(movement) > 0.0f) {
        movement = glm::normalize(movement);
    }

    constexpr float speed = 10.0f;
    camera.x += movement.x * speed * deltaTime;
    camera.y += movement.y * speed * deltaTime;
    camera.z += input.z * speed * deltaTime;
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
