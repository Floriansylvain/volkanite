#pragma once
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "Window.hpp"

#ifndef ENGINE_HPP
#define ENGINE_HPP

class Engine {
  public:
    explicit Engine(Window *_window);
    ~Engine();

    void init();
    void run() const;
    static void cleanup();

  private:
    std::vector<const char *> requiredDeviceExtension = {vk::KHRSwapchainExtensionName};

    Window &window;

    vk::raii::Context context;
    vk::raii::Instance instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::raii::Device device = nullptr;
    uint32_t queueIndex = ~0;
    vk::raii::Queue queue = nullptr;
    vk::raii::SurfaceKHR surface = nullptr;
    vk::raii::SwapchainKHR swapChain = nullptr;
    std::vector<vk::Image> swapChainImages;
    vk::SurfaceFormatKHR swapChainSurfaceFormat;
    vk::Extent2D swapChainExtent;
    std::vector<vk::raii::ImageView> swapChainImageViews;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline graphicsPipeline = nullptr;
    vk::raii::CommandPool commandPool = nullptr;
    vk::raii::CommandBuffer commandBuffer = nullptr;
    vk::raii::Semaphore presentCompleteSemaphore = nullptr;
    vk::raii::Semaphore renderFinishedSemaphore = nullptr;
    vk::raii::Fence drawFence = nullptr;

    bool isInitialized = false;

    void createInstance();
    void setupDebugMessenger();

    bool isDeviceSuitable(vk::raii::PhysicalDevice const &_physicalDevice);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSurface();

    static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats);
    static vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const &availablePresentModes);
    [[nodiscard]] vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const &capabilities) const;
    static uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const &surfaceCapabilities);
    void createSwapChain();
    void createImageViews();

    static std::vector<char> readFile(const std::string &filename);
    [[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char> &code) const;
    void createGraphicsPipeline();
    void createCommandPool();
    void createCommandBuffer();
    void transition_image_layout(uint32_t imageIndex, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                 vk::AccessFlags2 src_access_mask, vk::AccessFlags2 dst_access_mask,
                                 vk::PipelineStageFlags2 src_stage_mask, vk::PipelineStageFlags2 dst_stage_mask) const;
    void recordCommandBuffer(uint32_t imageIndex) const;
    void createSyncObjects();
    void drawFrame() const;
};

#endif
