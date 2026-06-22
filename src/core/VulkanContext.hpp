#ifndef VULKAN_CONTEXT_HPP
#define VULKAN_CONTEXT_HPP

#pragma once

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include <vector>

struct Window;

struct VulkanContext {
    vk::raii::Context context;

    vk::raii::Instance instance = nullptr;
    vk::raii::SurfaceKHR surface = nullptr;
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::raii::Device device = nullptr;

    uint32_t queueIndex = 0;
    vk::raii::Queue queue = nullptr;

    vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;

    void init(const Window &window);

  private:
    std::vector<const char *> requiredDeviceExtensions = {"VK_KHR_swapchain"};

    vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;

    void createInstance();
    void setupDebugMessenger();
    void createSurface(const Window &window);
    void pickPhysicalDevice();
    void createLogicalDevice();

    bool isDeviceSuitable(const vk::raii::PhysicalDevice &_physicalDevice);

    vk::SampleCountFlagBits getMaxUsableSampleCount() const;
};

#endif
