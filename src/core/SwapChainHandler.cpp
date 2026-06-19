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
    if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount)) {
        minImageCount = surfaceCapabilities.maxImageCount;
    }
    return minImageCount;
}

void SwapChainHandler::create() {
    const vk::SurfaceCapabilitiesKHR surfaceCapabilities = vkCtx.physicalDevice.getSurfaceCapabilitiesKHR(*vkCtx.surface);
    swapChainExtent = chooseSwapExtent(surfaceCapabilities);
    const uint32_t minImageCount = chooseSwapMinImageCount(surfaceCapabilities);

    const std::vector<vk::SurfaceFormatKHR> availableFormats = vkCtx.physicalDevice.getSurfaceFormatsKHR(*vkCtx.surface);
    swapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);

    const std::vector<vk::PresentModeKHR> availablePresentModes = vkCtx.physicalDevice.getSurfacePresentModesKHR(vkCtx.surface);

    vk::SwapchainCreateInfoKHR swapChainCreateInfo{};
    swapChainCreateInfo.surface = *vkCtx.surface;
    swapChainCreateInfo.minImageCount = minImageCount;
    swapChainCreateInfo.imageFormat = swapChainSurfaceFormat.format;
    swapChainCreateInfo.imageColorSpace = swapChainSurfaceFormat.colorSpace;
    swapChainCreateInfo.imageExtent = swapChainExtent;
    swapChainCreateInfo.imageArrayLayers = 1;
    swapChainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    swapChainCreateInfo.imageSharingMode = vk::SharingMode::eExclusive;
    swapChainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
    swapChainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    swapChainCreateInfo.presentMode = chooseSwapPresentMode(availablePresentModes);
    swapChainCreateInfo.clipped = true;

    swapChain = vk::raii::SwapchainKHR(vkCtx.device, swapChainCreateInfo);
    swapChainImages = swapChain.getImages();
}

void SwapChainHandler::createImageViews() {
    assert(swapChainImageViews.empty());

    swapChainImageViews.reserve(swapChainImages.size());
    for (auto const &image : swapChainImages) {
        swapChainImageViews.emplace_back(
            VulkanUtils::createImageView(vkCtx, image, swapChainSurfaceFormat.format, vk::ImageAspectFlagBits::eColor));
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

    throw EngineExceptions::Compatibility("failed to find supported format!");
}

vk::Format SwapChainHandler::findDepthFormat() const {
    using enum vk::Format;
    return findSupportedFormat({eD32Sfloat, eD32SfloatS8Uint, eD24UnormS8Uint}, vk::ImageTiling::eOptimal,
                               vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

void SwapChainHandler::createDepthResources() {
    const vk::Format depthFormat = findDepthFormat();
    std::tie(depthImage, depthImageMemory) =
        VulkanUtils::createImage(vkCtx, swapChainExtent.width, swapChainExtent.height, depthFormat, vk::ImageTiling::eOptimal,
                                 vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
    depthImageView = VulkanUtils::createImageView(vkCtx, depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth);
}

void SwapChainHandler::recreate() {
    while (SDL_GetWindowFlags(window.getSDL_window()) & SDL_WINDOW_MINIMIZED) {
        window.waitEvents();
    }
    vkCtx.device.waitIdle();

    cleanup();
    create();
    createImageViews();
    createDepthResources();
}

void SwapChainHandler::cleanup() {
    swapChainImageViews.clear();
    swapChain = nullptr;
}
