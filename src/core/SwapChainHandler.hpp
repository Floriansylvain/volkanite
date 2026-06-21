#ifndef SWAP_CHAIN_HANDLER_HPP
#define SWAP_CHAIN_HANDLER_HPP

#pragma once
#include "VulkanContext.hpp"

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

class SwapChainHandler {
  public:
    explicit SwapChainHandler(const VulkanContext &context, Window &window);
    ~SwapChainHandler();

    using Image = std::pair<vk::raii::Image, vk::raii::DeviceMemory>;

    [[nodiscard]] vk::Format findDepthFormat() const;

    vk::SurfaceFormatKHR surfaceFormat;
    vk::raii::SwapchainKHR swapChainKHR = nullptr;
    vk::Extent2D extent2D;
    std::vector<vk::Image> images;
    std::vector<vk::raii::ImageView> imageViews;

    vk::raii::Image depthImage = nullptr;
    vk::raii::DeviceMemory depthImageMemory = nullptr;
    vk::raii::ImageView depthImageView = nullptr;

    vk::raii::Image colorImage = nullptr;
    vk::raii::DeviceMemory colorImageMemory = nullptr;
    vk::raii::ImageView colorImageView = nullptr;

    void create();
    void createImageViews();
    void createColorResources();
    void createDepthResources();
    void recreate();
    void cleanup();

  private:
    const VulkanContext &vkCtx;
    Window &window;

    static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats);
    static vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const &availablePresentModes);
    static uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const &surfaceCapabilities);
    [[nodiscard]] vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const &capabilities) const;

    [[nodiscard]] vk::Format findSupportedFormat(const std::vector<vk::Format> &candidates, vk::ImageTiling tiling,
                                                 vk::FormatFeatureFlags features) const;
};

#endif