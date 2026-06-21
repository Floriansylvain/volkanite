#include "Engine.hpp"
#include "Constants.hpp"
#include "CullingUtils.hpp"
#include "Exceptions.hpp"
#include "MeshUtils.hpp"
#include "ShaderUtils.hpp"
#include "VulkanUtils.hpp"
#include "Window.hpp"

#include <SDL3_image/SDL_image.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

Engine::Engine(Window *_window, VulkanContext *_vkCtx)
    : window(*_window), vkCtx(*_vkCtx), swapChainHandler(vkCtx, window), instanceRenderer(vkCtx, MAX_FRAMES_IN_FLIGHT),
      textRenderer(vkCtx, MAX_FRAMES_IN_FLIGHT) {}

Engine::~Engine() = default;

void Engine::createGraphicsPipeline() {
    const vk::raii::ShaderModule shaderModule =
        ShaderUtils::createShaderModule(vkCtx, ShaderUtils::readFile("shaders/shader.spv"));

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
    textVertStageInfo.module = shaderModule;
    textVertStageInfo.pName = "vertMainText";

    vk::PipelineShaderStageCreateInfo textFragStageInfo{};
    textFragStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    textFragStageInfo.module = shaderModule;
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

void Engine::transition_image_layouts(const std::vector<TransitionImageLayoutCommand> &commands) const {
    std::vector<vk::ImageMemoryBarrier2> barriers;
    barriers.reserve(commands.size());

    for (const auto &command : commands) {
        vk::ImageSubresourceRange subresourceRange{};
        subresourceRange.aspectMask = command.image_aspect_flags;
        subresourceRange.levelCount = 1;
        subresourceRange.layerCount = 1;

        vk::ImageMemoryBarrier2 barrier{};
        barrier.srcStageMask = command.src_stage_mask;
        barrier.srcAccessMask = command.src_access_mask;
        barrier.dstStageMask = command.dst_stage_mask;
        barrier.dstAccessMask = command.dst_access_mask;
        barrier.oldLayout = command.old_layout;
        barrier.newLayout = command.new_layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = command.image;
        barrier.subresourceRange = subresourceRange;
        barriers.push_back(barrier);
    }

    vk::DependencyInfo dependencyInfo{};
    dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dependencyInfo.pImageMemoryBarriers = barriers.data();

    commandBuffers[frameIndex].pipelineBarrier2(dependencyInfo);
}

void Engine::recordCommandBuffer(const uint32_t imageIndex) {
    using enum vk::ImageLayout;
    using enum vk::PipelineStageFlagBits2;

    drawCallCount = 0;
    vertexCount = 0;

    commandBuffers[frameIndex].begin({});

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

    transition_image_layouts({colorTransition, msaaColorTransition, depthTransition});

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

    static auto startTime = std::chrono::high_resolution_clock::now();
    const float time = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();

    for (const auto &[position, mesh, texture, isInstanced, isVisible, rotation] : renderObjects) {
        if (!isVisible)
            continue;

        const auto &sets = textureDescriptorSets.at(texture);
        commandBuffers[frameIndex].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, *sets[frameIndex],
                                                      nullptr);

        std::vector<vk::DeviceSize> offsets = {0};
        commandBuffers[frameIndex].bindVertexBuffers(0, *mesh->unifiedBuffer, offsets);
        const vk::DeviceSize vertexSizeOffset = sizeof(Mesh::Vertex) * mesh->vertices.size();
        commandBuffers[frameIndex].bindIndexBuffer(*mesh->unifiedBuffer, vertexSizeOffset, vk::IndexType::eUint32);

        ModelPushConstant pc{};
        if (rotation != 0.f) {
            pc.model = glm::translate(glm::mat4(1.0f), position) *
                       rotate(glm::mat4(1.0f), time * glm::radians(90.0f) * rotation, glm::vec3(1.0f, 0.0f, 1.0f));
        } else {
            pc.model = glm::translate(glm::mat4(1.0f), position);
        }

        commandBuffers[frameIndex].pushConstants<ModelPushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, pc);

        commandBuffers[frameIndex].drawIndexed(static_cast<uint32_t>(mesh->indices.size()), 1, 0, 0, 0);
        ++drawCallCount;
        vertexCount += mesh->indices.size();
    }

    instanceRenderer.draw(commandBuffers[frameIndex], frameIndex, *pipelineLayout, textureDescriptorSets, isWireframe,
                          drawCallCount, vertexCount);

    float offset = 10.f;
    for (size_t i = 0; i < debugLines.size(); i++) {
        textRenderer.drawText(debugLines[i], 10.0f, i + offset, DEBUG_FONT_SIZE, glm::vec3(1.0f, 1.0f, 1.0f),
                              swapChainHandler.extent2D);
        offset += DEBUG_FONT_SIZE - 14.f;
    }
    textRenderer.render(commandBuffers[frameIndex], frameIndex);

    commandBuffers[frameIndex].endRendering();

    TransitionImageLayoutCommand presentTransition{};
    presentTransition.image = swapChainHandler.images[imageIndex], presentTransition.old_layout = eColorAttachmentOptimal;
    presentTransition.new_layout = ePresentSrcKHR;
    presentTransition.src_access_mask = vk::AccessFlagBits2::eColorAttachmentWrite;
    presentTransition.dst_access_mask = {};
    presentTransition.src_stage_mask = eColorAttachmentOutput;
    presentTransition.dst_stage_mask = eBottomOfPipe;
    presentTransition.image_aspect_flags = vk::ImageAspectFlagBits::eColor;

    transition_image_layouts({presentTransition});
    commandBuffers[frameIndex].end();
}

void Engine::createSyncObjects() {
    assert(presentCompleteSemaphores.empty() && renderFinishedSemaphores.empty() && inFlightFences.empty());

    for (auto _ : swapChainHandler.images) {
        renderFinishedSemaphores.emplace_back(vkCtx.device, vk::SemaphoreCreateInfo());
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        presentCompleteSemaphores.emplace_back(vkCtx.device, vk::SemaphoreCreateInfo());

        vk::FenceCreateInfo fenceInfo{};
        fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

        inFlightFences.emplace_back(vkCtx.device, fenceInfo);
    }
}

void Engine::drawFrame() {
    if (auto const fenceResult = vkCtx.device.waitForFences(*inFlightFences[frameIndex], vk::True, UINT64_MAX);
        fenceResult != vk::Result::eSuccess) {
        throw EngineExceptions::Render("Failed to wait for fence");
    }

    vk::Result result;
    uint32_t imageIndex;

    try {
        std::tie(result, imageIndex) =
            swapChainHandler.swapChainKHR.acquireNextImage(UINT64_MAX, *presentCompleteSemaphores[frameIndex], nullptr);
    } catch (const vk::OutOfDateKHRError &) {
        swapChainHandler.recreate();
        return;
    }
    if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
        assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
        throw EngineExceptions::Render("Failed to acquire swap chain image");
    }

    vkCtx.device.resetFences(*inFlightFences[frameIndex]);

    commandBuffers[frameIndex].reset();

    updateUniformBuffer(frameIndex);
    updateInstanceBuffers(frameIndex);

    recordCommandBuffer(imageIndex);

    constexpr vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    vk::SubmitInfo submitInfo{};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &*presentCompleteSemaphores[frameIndex];
    submitInfo.pWaitDstStageMask = &waitDestinationStageMask;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &*commandBuffers[frameIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &*renderFinishedSemaphores[imageIndex];

    vkCtx.queue.submit(submitInfo, *inFlightFences[frameIndex]);

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

    try {
        result = vkCtx.queue.presentKHR(presentInfoKHR);
    } catch (const vk::OutOfDateKHRError &) {
        framebufferResized = false;
        swapChainHandler.recreate();
        frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
        return;
    }

    if (result == vk::Result::eSuboptimalKHR || framebufferResized) {
        framebufferResized = false;
        swapChainHandler.recreate();
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
    std::unordered_map<std::string, MergedGroup> merged;

    for (const auto &sub : subMeshes) {
        if (sub.vertices.empty() || sub.filename.empty())
            continue;

        auto &group = merged[sub.filename];
        const auto vertexOffset = static_cast<uint32_t>(group.vertices.size());

        group.vertices.insert(group.vertices.end(), sub.vertices.begin(), sub.vertices.end());
        group.indices.reserve(group.indices.size() + sub.indices.size());
        for (const uint32_t idx : sub.indices) {
            group.indices.push_back(idx + vertexOffset);
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
            textureCache.emplace(filename, texture);
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

void Engine::init() {
    if (!window.isRunning()) {
        throw EngineExceptions::NotInitialized("Failed to run Engine : Window is not running");
    }

    window.setChangeCallback([this] { framebufferResized = true; });
    window.setWireframeCallback([this] { isWireframe = !isWireframe; });

    vkCtx.init(window);
    swapChainHandler.create();
    swapChainHandler.createImageViews();
    createDescriptorSetLayout();
    textRenderer.createDescriptorSetLayout();
    createGraphicsPipeline();
    createCommandPool();
    swapChainHandler.createColorResources();
    swapChainHandler.createDepthResources();

    createCameraUniformBuffer();
    createDescriptorPool();

    const FBXModel house = createFBXModel("models/House_scene_01.fbx", ".png");

    textRenderer.loadFont("textures/consolas.png", commandPool, 0.38, 0.2);

    // const auto brickTexture = std::make_shared<Texture>(vkCtx);
    // brickTexture->loadFromFile("textures/bricks.jpg", commandPool);

    // const auto cubeMesh = std::make_shared<Mesh>(vkCtx);
    // MeshUtils::generateCube(*cubeMesh, 1.f);
    // cubeMesh->createGeometryBuffers(commandPool);

    constexpr int SIZE = 300;
    constexpr int OFFSET = 50;
    for (int x = -SIZE / 2; x < SIZE / 2; x += OFFSET) {
        for (int y = -SIZE / 2; y < SIZE / 2; y += OFFSET) {
            for (int z = -SIZE / 2; z < SIZE / 2; z += OFFSET) {
                // RenderObject cube;
                // cube.mesh = cubeMesh;
                // cube.texture = brickTexture;
                // cube.position = {x, y, z};
                // cube.isInstanced = true;
                // cube.rotationSpeed =
                //     glm::sin(static_cast<float>(x)) + glm::sin(static_cast<float>(y)) + glm::sin(static_cast<float>(z));
                // addRenderObject(std::move(cube));

                placeFBXModel(house, glm::vec3(x, y, z), true);
            }
        }
    }

    // const auto cubeMesh2 = std::make_shared<Mesh>(vkCtx);
    // MeshUtils::generateCube(*cubeMesh2, 100.f);
    // cubeMesh2->createGeometryBuffers(commandPool);

    // RenderObject cube;
    // cube.mesh = cubeMesh2;
    // cube.texture = brickTexture;
    // cube.position = {0.f, 0.f, -150.f};
    // cube.isInstanced = false;
    // cube.rotationSpeed = 0.f;
    // addRenderObject(std::move(cube));

    instanceRenderer.build();
    createCommandBuffers();
    createSyncObjects();

    isInitialized = true;
}

void Engine::update() {
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

    constexpr float speed = 75.0f;
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

        window.pollEvents();
        update();
        drawFrame();

        const float frameTimeMs =
            std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - frameStart).count();

        frameTimeAccumulator += frameTimeMs;
        frameCountAccumulator++;

        if (frameTimeAccumulator >= 100.0f) {
            const float avgFrameTimeMs = frameTimeAccumulator / static_cast<float>(frameCountAccumulator);
            const float fps = 1000.0f / avgFrameTimeMs;

            debugLines.clear();
            debugLines.push_back(std::format("frametime: {:.2f}ms ({:.0f} fps)", avgFrameTimeMs, fps));
            debugLines.push_back(std::format("draws: {}", drawCallCount));
            debugLines.push_back(std::format("verts: {}", vertexCount));

            frameTimeAccumulator = 0.0f;
            frameCountAccumulator = 0;
        }
    }

    vkCtx.device.waitIdle();
}

void Engine::cleanup() { swapChainHandler.cleanup(); }
