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
    vk::raii::Queue queue = nullptr;
    vk::raii::SurfaceKHR surface = nullptr;
    vk::raii::SwapchainKHR swapChain = nullptr;
    vk::SurfaceFormatKHR swapChainSurfaceFormat;
    vk::Extent2D swapChainExtent;
    std::vector<vk::raii::ImageView> swapChainImageViews;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline graphicsPipeline = nullptr;

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
};

#endif
