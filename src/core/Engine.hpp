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
    Engine(Window *window);
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

    bool isInitialized = false;

    void createInstance();
    void setupDebugMessenger();
    bool isDeviceSuitable(vk::raii::PhysicalDevice const &_physicalDevice);
    void pickPhysicalDevice();
};

#endif
