#ifndef DEVICE_HPP
#define DEVICE_HPP

#include "Instance.hpp"
#include <optional>
#include <vulkan/vulkan_raii.hpp>

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;
};

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

// Hardware interface
class Device {
  public:
    Device(Instance &instance);
    ~Device();

    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;

    vk::raii::PhysicalDevice &getPhysicalDevice();
    vk::raii::Device &getLogicalDevice();
    vk::raii::Queue &getPresentQueue();

    QueueFamilyIndices getQueueFamilyIndices();
    SwapChainSupportDetails querySwapChainSupport(const vk::raii::PhysicalDevice &device) const;

  private:
    Instance &instance;
    std::vector<vk::raii::PhysicalDevice> physicalDevices;
    std::optional<vk::raii::PhysicalDevice> physicalDevice;
    std::optional<vk::raii::Device> logicalDevice;
    std::optional<vk::raii::Queue> presentQueue;

    QueueFamilyIndices indices;
    std::vector<const char *> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};

    int rateDeviceSuitability(const vk::raii::PhysicalDevice &device);
    QueueFamilyIndices findQueueFamilies(const vk::raii::PhysicalDevice &device) const;
    bool checkDeviceExtensionSupport(const vk::raii::PhysicalDevice &device) const;

    void initVkPhysicalDevice();
    void initVkLogicalDevice();
    void initVkPresentQueue();
};

#endif
