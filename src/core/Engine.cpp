#include "Engine.hpp"
#include "Constants.hpp"
#include "CullingUtils.hpp"
#include "Exceptions.hpp"
#include "MeshUtils.hpp"
#include "VulkanUtils.hpp"
#include "Window.hpp"

#include <SDL3_image/SDL_image.h>
#include <chrono>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

Engine::Engine(Window *_window, VulkanContext *_vkCtx) : window(*_window), vkCtx(*_vkCtx), swapChainHandler(vkCtx, window) {};

Engine::~Engine() = default;

std::vector<char> Engine::readFile(const std::string &filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw EngineExceptions::Shader("Failed to open file");
    }

    std::vector<char> buffer(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    file.close();

    return buffer;
}

vk::raii::ShaderModule Engine::createShaderModule(const std::vector<char> &code) const {
    if (code.size() % sizeof(uint32_t) != 0) {
        throw EngineExceptions::Shader("SPIR-V code size must be a multiple of 4 bytes.");
    }

    std::vector<uint32_t> alignedCode(code.size() / sizeof(uint32_t));
    std::memcpy(alignedCode.data(), code.data(), code.size());

    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = code.size();
    createInfo.pCode = alignedCode.data();

    vk::raii::ShaderModule shaderModule{vkCtx.device, createInfo};

    return shaderModule;
}

// TODO: Move InstanceBatch, InstanceData, buildInstanceBatches and updateInstanceBuffers OUT OF THERE
void Engine::createGraphicsPipeline() {
    const vk::raii::ShaderModule shaderModule = createShaderModule(readFile("shaders/shader.spv"));

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
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
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

    vk::StructureChain pipelineCreateInfoChain = {graphicsPipelineCreateInfo, pipelineRenderingCreateInfo};

    solidGraphicsPipeline =
        vk::raii::Pipeline(vkCtx.device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());

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

    vk::PipelineShaderStageCreateInfo instancedVertShaderStageInfo = vertShaderStageInfo;
    instancedVertShaderStageInfo.pName = "vertMainInstanced";
    const std::vector instancedShaderStages = {instancedVertShaderStageInfo, fragShaderStageInfo};

    vk::GraphicsPipelineCreateInfo instancedPipelineCreateInfo = graphicsPipelineCreateInfo;
    instancedPipelineCreateInfo.pStages = instancedShaderStages.data();
    instancedPipelineCreateInfo.pVertexInputState = &instancedVertexInputInfo;

    vk::StructureChain instancedChain = {instancedPipelineCreateInfo, pipelineRenderingCreateInfo};
    solidInstancedGraphicsPipeline =
        vk::raii::Pipeline(vkCtx.device, nullptr, instancedChain.get<vk::GraphicsPipelineCreateInfo>());

    rasterizer.polygonMode = vk::PolygonMode::eLine;

    wireframeGraphicsPipeline =
        vk::raii::Pipeline(vkCtx.device, nullptr, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());

    wireframeInstancedGraphicsPipeline =
        vk::raii::Pipeline(vkCtx.device, nullptr, instancedChain.get<vk::GraphicsPipelineCreateInfo>());
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

void Engine::transition_image_layout(transitionImageLayoutCommand const &command) const {
    vk::ImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = command.image_aspect_flags;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = 1;

    vk::ImageMemoryBarrier2 barrier = {};
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

    vk::DependencyInfo dependencyInfo{};
    dependencyInfo.dependencyFlags = {};
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &barrier;

    commandBuffers[frameIndex].pipelineBarrier2(dependencyInfo);
}

void Engine::recordCommandBuffer(const uint32_t imageIndex) {
    using enum vk::ImageLayout;
    using enum vk::PipelineStageFlagBits2;

    drawCallCount = 0;
    vertexCount = 0;

    commandBuffers[frameIndex].begin({});

    transitionImageLayoutCommand transitionCmd = {};

    transitionCmd.image = swapChainHandler.images[imageIndex];
    transitionCmd.old_layout = eUndefined;
    transitionCmd.new_layout = eColorAttachmentOptimal;
    transitionCmd.src_access_mask = {};
    transitionCmd.dst_access_mask = vk::AccessFlagBits2::eColorAttachmentWrite;
    transitionCmd.src_stage_mask = eColorAttachmentOutput;
    transitionCmd.dst_stage_mask = eColorAttachmentOutput;
    transitionCmd.image_aspect_flags = vk::ImageAspectFlagBits::eColor;

    transition_image_layout(transitionCmd);

    transitionCmd.image = *swapChainHandler.depthImage;
    transitionCmd.new_layout = eDepthAttachmentOptimal;
    transitionCmd.src_access_mask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    transitionCmd.dst_access_mask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    transitionCmd.src_stage_mask = eEarlyFragmentTests | eLateFragmentTests;
    transitionCmd.dst_stage_mask = eEarlyFragmentTests | eLateFragmentTests;
    transitionCmd.image_aspect_flags = vk::ImageAspectFlagBits::eDepth;

    transition_image_layout(transitionCmd);

    constexpr vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::RenderingAttachmentInfo attachmentInfo = {};
    attachmentInfo.imageView = swapChainHandler.imageViews[imageIndex];
    attachmentInfo.imageLayout = eColorAttachmentOptimal;
    attachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
    attachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
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
        if (isInstanced || !isVisible)
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

    if (isWireframe) {
        commandBuffers[frameIndex].bindPipeline(vk::PipelineBindPoint::eGraphics, *wireframeInstancedGraphicsPipeline);
    } else {
        commandBuffers[frameIndex].bindPipeline(vk::PipelineBindPoint::eGraphics, *solidInstancedGraphicsPipeline);
    }

    for (const auto &batch : instanceBatches) {
        if (batch.visibleInstanceCount == 0)
            continue;

        const auto &sets = textureDescriptorSets.at(batch.texture);
        commandBuffers[frameIndex].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, *sets[frameIndex],
                                                      nullptr);

        std::vector<vk::DeviceSize> vertexOffsets = {0};
        commandBuffers[frameIndex].bindVertexBuffers(0, *batch.mesh->unifiedBuffer, vertexOffsets);
        std::vector<vk::DeviceSize> instanceOffsets = {0};
        commandBuffers[frameIndex].bindVertexBuffers(1, *batch.buffers[frameIndex], instanceOffsets);
        const vk::DeviceSize vertexSizeOffset = sizeof(Mesh::Vertex) * batch.mesh->vertices.size();
        commandBuffers[frameIndex].bindIndexBuffer(*batch.mesh->unifiedBuffer, vertexSizeOffset, vk::IndexType::eUint32);

        commandBuffers[frameIndex].drawIndexed(static_cast<uint32_t>(batch.mesh->indices.size()), batch.visibleInstanceCount, 0,
                                               0, 0);
        drawCallCount++;
        vertexCount += static_cast<uint64_t>(batch.mesh->indices.size()) * batch.visibleInstanceCount;
    }

    commandBuffers[frameIndex].endRendering();

    transitionCmd.image = swapChainHandler.images[imageIndex];
    transitionCmd.old_layout = eColorAttachmentOptimal;
    transitionCmd.new_layout = ePresentSrcKHR;
    transitionCmd.src_access_mask = vk::AccessFlagBits2::eColorAttachmentWrite;
    transitionCmd.dst_access_mask = {};
    transitionCmd.src_stage_mask = eColorAttachmentOutput;
    transitionCmd.dst_stage_mask = eBottomOfPipe;
    transitionCmd.image_aspect_flags = vk::ImageAspectFlagBits::eColor;

    transition_image_layout(transitionCmd);
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

    auto [result, imageIndex] =
        swapChainHandler.swapChainKHR.acquireNextImage(UINT64_MAX, *presentCompleteSemaphores[frameIndex], nullptr);

    if (result == vk::Result::eErrorOutOfDateKHR) {
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

    result = vkCtx.queue.presentKHR(presentInfoKHR);
    if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR || framebufferResized) {
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
    renderObjects.push_back(std::move(object));
}

void Engine::buildInstanceBatches() {
    std::vector<InstanceBatch> batches;

    for (size_t i = 0; i < renderObjects.size(); i++) {
        const auto &obj = renderObjects[i];
        if (!obj.isInstanced)
            continue;

        if (auto it =
                std::ranges::find_if(batches, [&](const auto &b) { return b.mesh == obj.mesh && b.texture == obj.texture; });
            it == batches.end()) {
            InstanceBatch batch;
            batch.mesh = obj.mesh;
            batch.texture = obj.texture;
            batch.objectIndices.push_back(i);
            batches.push_back(std::move(batch));
        } else {
            it->objectIndices.push_back(i);
        }
    }

    for (auto &batch : batches) {
        batch.instanceCount = static_cast<uint32_t>(batch.objectIndices.size());
        batch.boundingRadius = batch.mesh->boundingRadius;

        const vk::DeviceSize bufferSize = sizeof(InstanceData) * batch.instanceCount;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            auto [buffer, memory] =
                VulkanUtils::createBuffer(vkCtx, bufferSize, vk::BufferUsageFlagBits::eVertexBuffer,
                                          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            batch.buffers.push_back(std::move(buffer));
            batch.buffersMemory.push_back(std::move(memory));
            batch.buffersMapped.push_back(batch.buffersMemory[i].mapMemory(0, bufferSize));
        }
    }

    instanceBatches = std::move(batches);
}

void Engine::updateInstanceBuffers(const uint32_t currentImage) {
    static auto startTime = std::chrono::high_resolution_clock::now();
    const float time = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();

    const float aspect =
        static_cast<float>(swapChainHandler.extent2D.width) / static_cast<float>(swapChainHandler.extent2D.height);

    constexpr float cullFovMargin = 8.0f;
    const glm::mat4 cullProj = Camera::projMatrix(aspect, 55.0f + cullFovMargin);

    const CullingUtils::Frustum frustum = CullingUtils::extractFrustum(cullProj * camera.viewMatrix());

    for (auto &batch : instanceBatches) {
        auto *dst = static_cast<InstanceData *>(batch.buffersMapped[currentImage]);
        uint32_t visibleCount = 0;

        for (const size_t i : batch.objectIndices) {
            const auto &obj = renderObjects[i];
            if (!sphereInFrustum(frustum, obj.position, batch.boundingRadius))
                continue;

            dst[visibleCount].position = obj.position;
            dst[visibleCount].rotation = time * obj.rotationSpeed;
            visibleCount++;
        }

        batch.visibleInstanceCount = visibleCount;
    }

    for (auto &obj : renderObjects) {
        if (obj.isInstanced)
            continue;
        obj.isVisible = sphereInFrustum(frustum, obj.position, obj.mesh->boundingRadius);
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
    createGraphicsPipeline();
    createCommandPool();
    swapChainHandler.createDepthResources();

    createCameraUniformBuffer();
    createDescriptorPool();

    const auto brickTexture = std::make_shared<Texture>(vkCtx);
    brickTexture->loadFromFile("textures/bricks.jpg", commandPool);

    const auto cubeMesh = std::make_shared<Mesh>(vkCtx);
    MeshUtility::generateCube(*cubeMesh, 1.f);
    cubeMesh->createGeometryBuffers(commandPool);

    constexpr int SIZE = 500;
    constexpr int OFFSET = 4;
    for (int x = -SIZE / 2; x < SIZE / 2; x += OFFSET) {
        for (int y = -SIZE / 2; y < SIZE / 2; y += OFFSET) {
            for (int z = -SIZE / 2; z < SIZE / 2; z += OFFSET) {
                RenderObject cube;
                cube.mesh = cubeMesh;
                cube.texture = brickTexture;
                cube.position = {x, y, z};
                cube.isInstanced = true;
                cube.rotationSpeed =
                    glm::sin(static_cast<float>(x)) + glm::sin(static_cast<float>(y)) + glm::sin(static_cast<float>(z));
                addRenderObject(std::move(cube));
            }
        }
    }

    const auto cubeMesh2 = std::make_shared<Mesh>(vkCtx);
    MeshUtility::generateCube(*cubeMesh2, 100.f);
    cubeMesh2->createGeometryBuffers(commandPool);

    RenderObject cube;
    cube.mesh = cubeMesh2;
    cube.texture = brickTexture;
    cube.position = {0.f, 0.f, -150.f};
    cube.isInstanced = false;
    cube.rotationSpeed = 0.f;
    addRenderObject(std::move(cube));

    buildInstanceBatches();
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

    constexpr float speed = 50.0f;
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

            std::string title = std::format("volkanite | {:.2f}ms ({:.0f} fps) | {} draws | {} verts", avgFrameTimeMs, fps,
                                            drawCallCount, vertexCount);
            SDL_SetWindowTitle(window.getSDL_window(), title.c_str());

            frameTimeAccumulator = 0.0f;
            frameCountAccumulator = 0;
        }
    }

    vkCtx.device.waitIdle();
}

void Engine::cleanup() { swapChainHandler.cleanup(); }
