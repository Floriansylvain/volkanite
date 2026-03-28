#include "SwapChain.hpp"

SwapChain::SwapChain(Window &window, Instance &instance, Device &device) : window(window), instance(instance), device(device) {
    initSwapChain();
    initImageViews();
}

SwapChain::~SwapChain() { cleanupSwapChain(); }

vk::SurfaceFormatKHR SwapChain::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats) {
    auto it = std::ranges::find_if(availableFormats, [](const auto &format) {
        return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });
    return it != availableFormats.end() ? *it : availableFormats[0];
}

vk::PresentModeKHR SwapChain::chooseSwapPresentMode(const std::vector<vk::PresentModeKHR> &availablePresentModes) {
    auto it = std::ranges::find(availablePresentModes, vk::PresentModeKHR::eMailbox);
    return it != availablePresentModes.end() ? *it : vk::PresentModeKHR::eFifo;
}

VkExtent2D SwapChain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return capabilities.currentExtent;

    int width, height;
    window.getSizeInPixels(&width, &height);

    VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height =
        std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
}

void SwapChain::initSwapChain() {
    SwapChainSupportDetails swapChainSupport = device.querySwapChainSupport(device.getPhysicalDevice());

    auto surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    auto presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D chosenExtent = chooseSwapExtent(swapChainSupport.capabilities);
    this->extent = vk::Extent2D{chosenExtent.width, chosenExtent.height};

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR createInfo{};
    createInfo.surface = *instance.getVkSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = this->extent;
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

    swapChainKHR.emplace(vk::raii::SwapchainKHR(device.getLogicalDevice(), createInfo));
    imageFormat = surfaceFormat.format;
    this->extent = vk::Extent2D{static_cast<uint32_t>(extent.width), static_cast<uint32_t>(extent.height)};

    uint32_t swapchainImageCount = 0;
    vkGetSwapchainImagesKHR(*device.getLogicalDevice(), *swapChainKHR.value(), &swapchainImageCount, nullptr);
    std::vector<VkImage> vkImages(swapchainImageCount);
    vkGetSwapchainImagesKHR(*device.getLogicalDevice(), *swapChainKHR.value(), &swapchainImageCount, vkImages.data());
    images.assign(vkImages.begin(), vkImages.end());
}

void SwapChain::initImageViews() {
    imageViews.reserve(images.size());

    for (const auto &image : images) {
        vk::ImageViewCreateInfo createInfo{};
        createInfo.image = image;
        createInfo.viewType = vk::ImageViewType::e2D;
        createInfo.format = imageFormat;
        createInfo.components.r = vk::ComponentSwizzle::eIdentity;
        createInfo.components.g = vk::ComponentSwizzle::eIdentity;
        createInfo.components.b = vk::ComponentSwizzle::eIdentity;
        createInfo.components.a = vk::ComponentSwizzle::eIdentity;
        createInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        imageViews.emplace_back(device.getLogicalDevice(), createInfo);
    }
}

void SwapChain::cleanupSwapChain() {
    imageViews.clear();
    swapChainKHR = nullptr;
}

void SwapChain::recreateSwapChain() {
    int width = 0, height = 0;
    window.getSizeInPixels(&width, &height);

    while (width == 0 || height == 0) {
        window.getSizeInPixels(&width, &height);
        window.waitEvents();
    }

    device.getLogicalDevice().waitIdle();

    cleanupSwapChain();

    initSwapChain();
    initImageViews();
}

vk::raii::SwapchainKHR *SwapChain::getSwapChainKHR() { return &swapChainKHR.value(); }

vk::SwapchainKHR SwapChain::getRawHandle() { return *swapChainKHR.value(); }

vk::Format *SwapChain::getImageFormat() { return &imageFormat; }

vk::Extent2D *SwapChain::getExtent() { return &extent; }

const std::vector<vk::Image> &SwapChain::getImages() const { return images; }

const std::vector<vk::raii::ImageView> &SwapChain::getImageViews() const { return imageViews; }