#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_beta.h>
#include <vulkan/vulkan_raii.hpp>

#include <core/Device.hpp>
#include <core/Instance.hpp>
#include <core/Pipeline.hpp>
#include <core/SwapChain.hpp>
#include <core/Window.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <ranges>
#include <set>
#include <vector>

constexpr int MAX_FRAMES_IN_FLIGHT = 2;

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

vk::raii::CommandPool initCommandPool(const vk::raii::Device &device, const QueueFamilyIndices &indices) {
    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

    return vk::raii::CommandPool(device, poolInfo);
}

std::vector<vk::raii::CommandBuffer> initCommandBuffers(const vk::raii::Device &device,
                                                        const vk::raii::CommandPool &commandPool) {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    return vk::raii::CommandBuffers(device, allocInfo);
}

void transition_image_layout(uint32_t imageIndex, vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                             vk::AccessFlags2 srcAccessMask, vk::AccessFlags2 dstAccessMask,
                             vk::PipelineStageFlags2 srcStageMask, vk::PipelineStageFlags2 dstStageMask,
                             std::vector<vk::Image> swapChainImages, const vk::raii::CommandBuffer &commandBuffer) {
    vk::ImageMemoryBarrier2 barrier = {};
    barrier.srcStageMask = srcStageMask;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstStageMask = dstStageMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapChainImages[imageIndex];

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

void recordCommandBuffer(uint32_t imageIndex, std::vector<vk::Image> swapChainImages,
                         const std::vector<vk::raii::CommandBuffer> &commandBuffers, const vk::raii::Pipeline &graphicsPipeline,
                         const std::vector<vk::raii::ImageView> &imageViews, const vk::Extent2D &extent, uint32_t frameIndex) {
    auto &commandBuffer = commandBuffers[frameIndex];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    commandBuffer.begin(beginInfo);

    transition_image_layout(imageIndex, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
                            vk::AccessFlagBits2::eNone, vk::AccessFlagBits2::eColorAttachmentWrite,
                            vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            swapChainImages, commandBuffer);

    vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::RenderingAttachmentInfo attachmentInfo{};
    attachmentInfo.imageView = imageViews[imageIndex];
    attachmentInfo.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    attachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
    attachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
    attachmentInfo.clearValue = clearColor;

    vk::RenderingInfo renderingInfo{};
    renderingInfo.renderArea = {.offset = {0, 0}, .extent = extent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &attachmentInfo;

    commandBuffer.beginRendering(renderingInfo);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
    commandBuffer.setViewport(
        0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));

    vk::Rect2D scissor{{0, 0}, extent};
    commandBuffer.setScissor(0, scissor);

    commandBuffer.draw(3, 1, 0, 0);
    commandBuffer.endRendering();

    transition_image_layout(imageIndex, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
                            vk::AccessFlagBits2::eColorAttachmentWrite, {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                            vk::PipelineStageFlagBits2::eBottomOfPipe, swapChainImages, commandBuffer);

    commandBuffer.end();
}

struct SyncObjects {
    std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
    std::vector<vk::raii::Fence> inFlightFences;
};

SyncObjects initSyncObjects(const vk::raii::Device &device, uint32_t imageCount) {
    SyncObjects syncObjects{};

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        syncObjects.presentCompleteSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
        syncObjects.inFlightFences.emplace_back(device, vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled});
    }

    for (size_t i = 0; i < imageCount; i++) {
        syncObjects.renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
    }

    return syncObjects;
}

int main() {
    Window window("volkanite", 800, 600);
    Instance instance(window, enableValidationLayers);
    Device device(instance);
    SwapChain swapChain(window, instance, device);
    Pipeline pipeline(device, swapChain);

    vk::raii::CommandPool commandPool = initCommandPool(device.getLogicalDevice(), device.getQueueFamilyIndices());
    std::vector<vk::raii::CommandBuffer> commandBuffers = initCommandBuffers(device.getLogicalDevice(), commandPool);

    SyncObjects syncObjects = initSyncObjects(device.getLogicalDevice(), swapChain.getImages().size());
    uint32_t frameIndex = 0;

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
        }

        auto fenceResult =
            device.getLogicalDevice().waitForFences(*syncObjects.inFlightFences[frameIndex], vk::True, UINT64_MAX);
        if (fenceResult != vk::Result::eSuccess) {
            throw std::runtime_error("failed to wait for fence!");
        }
        device.getLogicalDevice().resetFences(*syncObjects.inFlightFences[frameIndex]);

        auto [result, imageIndex] = swapChain.getSwapChainKHR()->acquireNextImage(
            UINT64_MAX, *syncObjects.presentCompleteSemaphores[frameIndex], nullptr);

        commandBuffers[frameIndex].reset();
        recordCommandBuffer(imageIndex, swapChain.getImages(), commandBuffers, *pipeline.getGraphicsPipeline(),
                            swapChain.getImageViews(), *swapChain.getExtent(), frameIndex);

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

        frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    };

    device.getLogicalDevice().waitIdle();

    return 0;
}
