#include "SwapChainHandler.hpp"
#include "Exceptions.hpp"
#include "VulkanUtils.hpp"
#include "Window.hpp"

#include <SDL3/SDL_video.h>

SwapChainHandler::SwapChainHandler(const VulkanContext &context, Window &window) : vkCtx(context), window(window) {}

SwapChainHandler::~SwapChainHandler() = default;

vk::SurfaceFormatKHR SwapChainHandler::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats) {
    const auto formatIt = std::ranges::find_if(availableFormats, [](const auto &format) {
        return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });
    return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
}

vk::PresentModeKHR SwapChainHandler::chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const &availablePresentModes) {
    assert(
        std::ranges::any_of(availablePresentModes, [](auto presentMode) { return presentMode == vk::PresentModeKHR::eFifo; }));
    return std::ranges::any_of(availablePresentModes,
                               [](const vk::PresentModeKHR value) { return vk::PresentModeKHR::eMailbox == value; })
               ? vk::PresentModeKHR::eMailbox
               : vk::PresentModeKHR::eFifo;
}

vk::Extent2D SwapChainHandler::chooseSwapExtent(vk::SurfaceCapabilitiesKHR const &capabilities) const {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    int width;
    int height;
    window.getSizeInPixels(&width, &height);

    return {std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)};
}

uint32_t SwapChainHandler::chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const &surfaceCapabilities) {
    auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
    if (0 < surfaceCapabilities.maxImageCount && surfaceCapabilities.maxImageCount < minImageCount) {
        minImageCount = surfaceCapabilities.maxImageCount;
    }
    return minImageCount;
}

void SwapChainHandler::create() {
    const vk::SurfaceCapabilitiesKHR surfaceCapabilities = vkCtx.physicalDevice.getSurfaceCapabilitiesKHR(*vkCtx.surface);
    extent2D = chooseSwapExtent(surfaceCapabilities);
    const uint32_t minImageCount = chooseSwapMinImageCount(surfaceCapabilities);

    const std::vector<vk::SurfaceFormatKHR> availableFormats = vkCtx.physicalDevice.getSurfaceFormatsKHR(*vkCtx.surface);
    surfaceFormat = chooseSwapSurfaceFormat(availableFormats);

    const std::vector<vk::PresentModeKHR> availablePresentModes = vkCtx.physicalDevice.getSurfacePresentModesKHR(vkCtx.surface);
    presentMode = chooseSwapPresentMode(availablePresentModes);

    vk::SwapchainCreateInfoKHR swapChainCreateInfo{};
    swapChainCreateInfo.surface = *vkCtx.surface;
    swapChainCreateInfo.minImageCount = minImageCount;
    swapChainCreateInfo.imageFormat = surfaceFormat.format;
    swapChainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapChainCreateInfo.imageExtent = extent2D;
    swapChainCreateInfo.imageArrayLayers = 1;
    swapChainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    swapChainCreateInfo.imageSharingMode = vk::SharingMode::eExclusive;
    swapChainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
    swapChainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    swapChainCreateInfo.presentMode = presentMode;
    swapChainCreateInfo.clipped = true;

    swapChainKHR = vk::raii::SwapchainKHR(vkCtx.device, swapChainCreateInfo);
    images = swapChainKHR.getImages();
}

void SwapChainHandler::createImageViews() {
    assert(imageViews.empty());

    imageViews.reserve(images.size());
    for (auto const &image : images) {
        imageViews.emplace_back(
            VulkanUtils::createImageView(vkCtx, image, surfaceFormat.format, vk::ImageAspectFlagBits::eColor, 1));
    }
}

vk::Format SwapChainHandler::findSupportedFormat(const std::vector<vk::Format> &candidates, vk::ImageTiling tiling,
                                                 const vk::FormatFeatureFlags features) const {
    for (const auto format : candidates) {

        if (const vk::FormatProperties props = vkCtx.physicalDevice.getFormatProperties(format);
            (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) ||
            (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features)) {
            return format;
        }
    }

    throw EngineExceptions::Compatibility("Failed to find supported format.");
}

vk::Format SwapChainHandler::findDepthFormat() const {
    using enum vk::Format;
    return findSupportedFormat({eD32Sfloat, eD32SfloatS8Uint, eD24UnormS8Uint}, vk::ImageTiling::eOptimal,
                               vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

void SwapChainHandler::createColorResources() {
    const vk::Format colorFormat = surfaceFormat.format;

    std::tie(colorImage, colorImageMemory) = VulkanUtils::createImage(
        vkCtx, {extent2D.width, extent2D.height, 1, vkCtx.msaaSamples, colorFormat, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal});
    colorImageView = VulkanUtils::createImageView(vkCtx, colorImage, colorFormat, vk::ImageAspectFlagBits::eColor, 1);
}

void SwapChainHandler::createDepthResources() {
    const vk::Format depthFormat = findDepthFormat();
    std::tie(depthImage, depthImageMemory) = VulkanUtils::createImage(
        vkCtx, {extent2D.width, extent2D.height, 1, vkCtx.msaaSamples, depthFormat, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal});
    depthImageView = VulkanUtils::createImageView(vkCtx, depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);
}

void SwapChainHandler::recreate() {
    while (SDL_GetWindowFlags(window.getSDL_window()) & SDL_WINDOW_MINIMIZED) {
        window.waitEvents();
    }
    vkCtx.device.waitIdle();

    cleanup();
    create();
    createImageViews();
    createColorResources();
    createDepthResources();
}

void SwapChainHandler::cleanup() {
    imageViews.clear();
    swapChainKHR = nullptr;
}
