#include "Renderer.hpp"
#include <vulkan/vulkan_raii.hpp>

Renderer::Renderer(Device &device, SwapChain &swapChain, Pipeline &pipeline)
    : device(device), swapChain(swapChain), pipeline(pipeline), frameIndex(0), framebufferResized(false) {
    initCommandPool();
    initCommandBuffer();
    initSyncObjects();
}

Renderer::~Renderer() {}

void Renderer::initCommandPool() {
    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsFamily.value();

    commandPool.emplace(vk::raii::CommandPool(device.getLogicalDevice(), poolInfo));
}

void Renderer::initCommandBuffer() {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = commandPool.value();
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    commandBuffers = vk::raii::CommandBuffers(device.getLogicalDevice(), allocInfo);
}

void Renderer::transition_image_layout(uint32_t imageIndex, vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                                       vk::AccessFlags2 srcAccessMask, vk::AccessFlags2 dstAccessMask,
                                       vk::PipelineStageFlags2 srcStageMask, vk::PipelineStageFlags2 dstStageMask,
                                       const vk::raii::CommandBuffer &commandBuffer) {
    vk::ImageMemoryBarrier2 barrier = {};
    barrier.srcStageMask = srcStageMask;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstStageMask = dstStageMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapChain.getImages()[imageIndex];

    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vk::DependencyInfo dependencyInfo{};
    dependencyInfo.dependencyFlags = {};
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &barrier;

    commandBuffer.pipelineBarrier2(dependencyInfo);
}

void Renderer::recordCommandBuffer(uint32_t imageIndex) {
    auto &commandBuffer = commandBuffers[frameIndex];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    commandBuffer.begin(beginInfo);

    transition_image_layout(imageIndex, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
                            vk::AccessFlagBits2::eNone, vk::AccessFlagBits2::eColorAttachmentWrite,
                            vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            commandBuffer);

    vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::RenderingAttachmentInfo attachmentInfo{};
    attachmentInfo.imageView = swapChain.getImageViews()[imageIndex];
    attachmentInfo.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    attachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
    attachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
    attachmentInfo.clearValue = clearColor;

    auto extent = swapChain.getExtent();

    vk::RenderingInfo renderingInfo{};
    renderingInfo.renderArea = {.offset = {0, 0}, .extent = *extent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &attachmentInfo;

    commandBuffer.beginRendering(renderingInfo);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.getGraphicsPipeline());
    commandBuffer.setViewport(
        0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent->width), static_cast<float>(extent->height), 0.0f, 1.0f));

    vk::Rect2D scissor{{0, 0}, *extent};
    commandBuffer.setScissor(0, scissor);

    commandBuffer.bindVertexBuffers(0, **pipeline.getVertexBuffer(), {0});
    commandBuffer.draw(3, 1, 0, 0);
    commandBuffer.endRendering();

    transition_image_layout(imageIndex, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
                            vk::AccessFlagBits2::eColorAttachmentWrite, {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            vk::PipelineStageFlagBits2::eBottomOfPipe, commandBuffer);

    commandBuffer.end();
}

void Renderer::initSyncObjects() {
    SyncObjects _syncObjects{};

    auto &logicalDevice = device.getLogicalDevice();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        _syncObjects.presentCompleteSemaphores.emplace_back(logicalDevice, vk::SemaphoreCreateInfo());
        _syncObjects.inFlightFences.emplace_back(logicalDevice, vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled});
    }

    for (size_t i = 0; i < swapChain.getImages().size(); i++) {
        _syncObjects.renderFinishedSemaphores.emplace_back(logicalDevice, vk::SemaphoreCreateInfo());
    }

    syncObjects = std::move(_syncObjects);
}

void Renderer::render() {
    auto fenceResult = device.getLogicalDevice().waitForFences(*syncObjects.inFlightFences[frameIndex], vk::True, UINT64_MAX);
    if (fenceResult != vk::Result::eSuccess) {
        throw std::runtime_error("failed to wait for fence!");
    }

    auto [result, imageIndex] =
        swapChain.getSwapChainKHR()->acquireNextImage(UINT64_MAX, *syncObjects.presentCompleteSemaphores[frameIndex], nullptr);

    if (result == vk::Result::eErrorOutOfDateKHR) {
        swapChain.recreateSwapChain();
        return;
    } else if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
        assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    device.getLogicalDevice().resetFences(*syncObjects.inFlightFences[frameIndex]);
    commandBuffers[frameIndex].reset();
    recordCommandBuffer(imageIndex);

    vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    vk::SubmitInfo submitInfo{};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &*syncObjects.presentCompleteSemaphores[frameIndex];
    submitInfo.pWaitDstStageMask = &waitDestinationStageMask;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &*commandBuffers[frameIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &*syncObjects.renderFinishedSemaphores[imageIndex];

    device.getPresentQueue().submit(submitInfo, *syncObjects.inFlightFences[frameIndex]);

    vk::SwapchainKHR rawHandle = swapChain.getRawHandle();
    vk::PresentInfoKHR presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &rawHandle;
    presentInfo.pWaitSemaphores = &*syncObjects.renderFinishedSemaphores[imageIndex];
    presentInfo.pImageIndices = &imageIndex;

    result = device.getPresentQueue().presentKHR(presentInfo);
    if ((result == vk::Result::eSuboptimalKHR) || (result == vk::Result::eErrorOutOfDateKHR) || framebufferResized) {
        framebufferResized = false;
        swapChain.recreateSwapChain();
    } else {
        assert(result == vk::Result::eSuccess);
    }

    frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Renderer::setFramebufferResized() { framebufferResized = true; }
