#ifndef SWAPCHAIN_HPP
#define SWAPCHAIN_HPP

#include <vulkan/vulkan_raii.hpp>

#include "Device.hpp"
#include "Instance.hpp"
#include "Window.hpp"

// Image Buffering
class SwapChain {
  public:
    SwapChain(Window &window, Instance &instance, Device &device);
    ~SwapChain();

    SwapChain(const SwapChain &) = delete;
    SwapChain &operator=(const SwapChain &) = delete;

    vk::raii::SwapchainKHR *getSwapChainKHR();
    vk::SwapchainKHR getRawHandle();
    vk::Format *getImageFormat();
    vk::Extent2D *getExtent();
    const std::vector<vk::Image> &getImages() const;
    const std::vector<vk::raii::ImageView> &getImageViews() const;

    void recreateSwapChain();

  private:
    std::optional<vk::raii::SwapchainKHR> swapChainKHR;
    vk::Format imageFormat;
    vk::Extent2D extent;
    std::vector<vk::Image> images;
    std::vector<vk::raii::ImageView> imageViews;

    Window &window;
    Instance &instance;
    Device &device;

    vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats);
    vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR> &availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities);

    void initSwapChain();
    void initImageViews();
    void cleanupSwapChain();
};

#endif
