#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_beta.h>
#include <vulkan/vulkan_raii.hpp>

#include <core/Device.hpp>
#include <core/Instance.hpp>
#include <core/Window.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <ranges>
#include <set>
#include <vector>

const auto APPLICATION_NAME = "volkanite";
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                    VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                                    void *pUserData) {
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    }
    return VK_FALSE;
}

vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats) {
    auto it = std::ranges::find_if(availableFormats, [](const auto &format) {
        return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });
    return it != availableFormats.end() ? *it : availableFormats[0];
}

vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR> &availablePresentModes) {
    auto it = std::ranges::find(availablePresentModes, vk::PresentModeKHR::eMailbox);
    return it != availablePresentModes.end() ? *it : vk::PresentModeKHR::eFifo;
}

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities, SDL_Window *window) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return capabilities.currentExtent;

    int width, height;
    SDL_GetWindowSizeInPixels(window, &width, &height);

    VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height =
        std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
}

struct SwapChainDetails {
    vk::raii::SwapchainKHR swapChain = nullptr;
    vk::Format imageFormat = vk::Format::eUndefined;
    vk::Extent2D extent = vk::Extent2D{0, 0};
    std::vector<vk::Image> images;
};

SwapChainDetails initSwapChain(SDL_Window *window, Device &device, Instance &instance) {
    SwapChainSupportDetails swapChainSupport = device.querySwapChainSupport(device.getPhysicalDevice());

    auto surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    auto presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities, window);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR createInfo{};
    createInfo.surface = *instance.getVkSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = vk::Extent2D{static_cast<uint32_t>(extent.width), static_cast<uint32_t>(extent.height)};
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    uint32_t queueFamilyIndices[] = {device.getQueueFamilyIndices().graphicsFamily.value(),
                                     device.getQueueFamilyIndices().presentFamily.value()};
    if (device.getQueueFamilyIndices().graphicsFamily != device.getQueueFamilyIndices().presentFamily) {
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = static_cast<vk::SurfaceTransformFlagBitsKHR>(swapChainSupport.capabilities.currentTransform);
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = vk::True;
    createInfo.oldSwapchain = nullptr;

    SwapChainDetails details;
    details.swapChain = vk::raii::SwapchainKHR(device.getLogicalDevice(), createInfo);
    details.imageFormat = surfaceFormat.format;
    details.extent = vk::Extent2D{static_cast<uint32_t>(extent.width), static_cast<uint32_t>(extent.height)};

    uint32_t swapchainImageCount = 0;
    vkGetSwapchainImagesKHR(*device.getLogicalDevice(), *details.swapChain, &swapchainImageCount, nullptr);
    std::vector<VkImage> vkImages(swapchainImageCount);
    vkGetSwapchainImagesKHR(*device.getLogicalDevice(), *details.swapChain, &swapchainImageCount, vkImages.data());
    details.images.assign(vkImages.begin(), vkImages.end());

    return details;
}

std::vector<vk::raii::ImageView> initImageViews(const vk::raii::Device &logicalDevice, const SwapChainDetails &details) {
    std::vector<vk::raii::ImageView> swapChainImageViews;
    swapChainImageViews.reserve(details.images.size());

    for (const auto &image : details.images) {
        vk::ImageViewCreateInfo createInfo{};
        createInfo.image = image;
        createInfo.viewType = vk::ImageViewType::e2D;
        createInfo.format = details.imageFormat;
        createInfo.components.r = vk::ComponentSwizzle::eIdentity;
        createInfo.components.g = vk::ComponentSwizzle::eIdentity;
        createInfo.components.b = vk::ComponentSwizzle::eIdentity;
        createInfo.components.a = vk::ComponentSwizzle::eIdentity;
        createInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        swapChainImageViews.emplace_back(logicalDevice, createInfo);
    }

    return swapChainImageViews;
}

static std::vector<char> readFile(const std::string &filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

vk::raii::ShaderModule createShaderModule(const std::vector<char> &code, const vk::raii::Device &logicalDevice) {
    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

    return vk::raii::ShaderModule(logicalDevice, createInfo);
}

vk::raii::Pipeline initGraphicsPipeline(const vk::raii::Device &logicalDevice, vk::Format swapChainImageFormat,
                                        const vk::raii::PipelineLayout &pipelineLayout, const vk::Extent2D &extent) {
    auto vertShaderCode = readFile("shaders/vert.spv");
    auto fragShaderCode = readFile("shaders/frag.spv");

    vk::raii::ShaderModule vertShaderModule = createShaderModule(vertShaderCode, logicalDevice);
    vk::raii::ShaderModule fragShaderModule = createShaderModule(fragShaderCode, logicalDevice);

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    vertShaderStageInfo.module = *vertShaderModule;
    vertShaderStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    fragShaderStageInfo.module = *fragShaderModule;
    fragShaderStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = vk::False;

    vk::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D{0, 0};
    scissor.extent = extent;

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.depthClampEnable = vk::False;
    rasterizer.rasterizerDiscardEnable = vk::False;
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = vk::CullModeFlagBits::eBack;
    rasterizer.frontFace = vk::FrontFace::eClockwise;
    rasterizer.depthBiasEnable = vk::False;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable = vk::False;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = vk::False;
    multisampling.alphaToOneEnable = vk::False;

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = vk::True;
    colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = vk::False;
    colorBlending.logicOp = vk::LogicOp::eCopy;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    vk::PipelineRenderingCreateInfo renderingCreateInfo{};
    renderingCreateInfo.colorAttachmentCount = 1;
    renderingCreateInfo.pColorAttachmentFormats = &swapChainImageFormat;

    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.pNext = &renderingCreateInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = nullptr;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = *pipelineLayout;
    pipelineInfo.renderPass = nullptr;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    return vk::raii::Pipeline(logicalDevice, nullptr, pipelineInfo);
}

vk::raii::PipelineLayout initPipelineLayout(const vk::raii::Device &logicalDevice) {
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    return vk::raii::PipelineLayout(logicalDevice, pipelineLayoutInfo);
}

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
    std::vector<const char *> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};

    Window window(APPLICATION_NAME, 800, 600);
    Instance instance(window, enableValidationLayers);
    Device device(&instance);

    SwapChainDetails swapChainDetails = initSwapChain(window.getSDL_window(), device, instance);

    std::vector<vk::raii::ImageView> swapChainImageViews = initImageViews(device.getLogicalDevice(), swapChainDetails);

    vk::raii::PipelineLayout pipelineLayout = initPipelineLayout(device.getLogicalDevice());
    vk::raii::Pipeline graphicsPipeline =
        initGraphicsPipeline(device.getLogicalDevice(), swapChainDetails.imageFormat, pipelineLayout, swapChainDetails.extent);

    vk::raii::CommandPool commandPool = initCommandPool(device.getLogicalDevice(), device.getQueueFamilyIndices());
    std::vector<vk::raii::CommandBuffer> commandBuffers = initCommandBuffers(device.getLogicalDevice(), commandPool);

    SyncObjects syncObjects = initSyncObjects(device.getLogicalDevice(), swapChainDetails.images.size());
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

        auto [result, imageIndex] = swapChainDetails.swapChain.acquireNextImage(
            UINT64_MAX, *syncObjects.presentCompleteSemaphores[frameIndex], nullptr);

        commandBuffers[frameIndex].reset();
        recordCommandBuffer(imageIndex, swapChainDetails.images, commandBuffers, graphicsPipeline, swapChainImageViews,
                            swapChainDetails.extent, frameIndex);

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

        vk::PresentInfoKHR presentInfo{};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &*swapChainDetails.swapChain;
        presentInfo.pWaitSemaphores = &*syncObjects.renderFinishedSemaphores[imageIndex];
        presentInfo.pImageIndices = &imageIndex;

        result = device.getPresentQueue().presentKHR(presentInfo);

        frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    };

    device.getLogicalDevice().waitIdle();

    return 0;
}
