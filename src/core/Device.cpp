#include "Device.hpp"
#include "Instance.hpp"

#include <set>
#include <stdexcept>
#include <string_view>

Device::Device(Instance &instance) : instance(instance), physicalDevices(instance.getPhysicalDevices()) {
    initVkPhysicalDevice();
    initVkLogicalDevice();
    initVkPresentQueue();
}

Device::~Device() {}

void Device::initVkPhysicalDevice() {
    if (physicalDevices.empty()) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    int bestScore = -1;
    size_t bestIndex = 0;

    for (size_t i = 0; i < physicalDevices.size(); ++i) {
        int score = rateDeviceSuitability(physicalDevices[i]);
        if (score > bestScore) {
            bestScore = score;
            bestIndex = i;
        }
    }

    if (bestScore > 0) {
        physicalDevice = std::move(physicalDevices[bestIndex]);
        indices = findQueueFamilies(*physicalDevice);
    } else {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

void Device::initVkLogicalDevice() {
    auto supportedExtensions = physicalDevice->enumerateDeviceExtensionProperties();
    for (const auto &ext : supportedExtensions) {
        if (std::string_view(ext.extensionName) == "VK_KHR_portability_subset") {
            deviceExtensions.push_back("VK_KHR_portability_subset");
            break;
        }
    }

    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        vk::DeviceQueueCreateInfo queueCreateInfo({}, queueFamily, 1, &queuePriority);
        queueCreateInfos.push_back(queueCreateInfo);
    }

    vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures(vk::True);
    vk::PhysicalDeviceSynchronization2Features synchronization2Features(vk::True, &dynamicRenderingFeatures);
    vk::PhysicalDeviceFeatures deviceFeatures{};

    vk::DeviceCreateInfo createInfo({}, static_cast<uint32_t>(queueCreateInfos.size()), queueCreateInfos.data(), 0, nullptr,
                                    static_cast<uint32_t>(deviceExtensions.size()), deviceExtensions.data(), &deviceFeatures,
                                    &synchronization2Features);

    logicalDevice.emplace(*physicalDevice, createInfo);
}

void Device::initVkPresentQueue() { presentQueue.emplace(*logicalDevice, indices.graphicsFamily.value(), 0); }

QueueFamilyIndices Device::findQueueFamilies(const vk::raii::PhysicalDevice &device) const {
    QueueFamilyIndices localIndices;
    auto queueFamilies = device.getQueueFamilyProperties();

    for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
        if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics)
            localIndices.graphicsFamily = i;

        if (device.getSurfaceSupportKHR(i, *instance.getVkSurface()))
            localIndices.presentFamily = i;

        if (localIndices.isComplete())
            break;
    }
    return localIndices;
}

int Device::rateDeviceSuitability(const vk::raii::PhysicalDevice &device) {
    auto deviceProperties = device.getProperties();
    QueueFamilyIndices localIndices = findQueueFamilies(device);
    bool extensionsSupported = checkDeviceExtensionSupport(device);

    int score = 0;
    if (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
        score += 1000;

    if (!localIndices.isComplete())
        return 0;
    if (!extensionsSupported)
        return 0;

    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
    if (swapChainSupport.formats.empty() || swapChainSupport.presentModes.empty())
        return 0;

    return score + 10;
}

bool Device::checkDeviceExtensionSupport(const vk::raii::PhysicalDevice &device) const {
    auto availableExtensions = device.enumerateDeviceExtensionProperties();
    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto &extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }
    return requiredExtensions.empty();
}

SwapChainSupportDetails Device::querySwapChainSupport(const vk::raii::PhysicalDevice &device) const {
    SwapChainSupportDetails details;
    details.capabilities = device.getSurfaceCapabilitiesKHR(*instance.getVkSurface());
    details.formats = device.getSurfaceFormatsKHR(*instance.getVkSurface());
    details.presentModes = device.getSurfacePresentModesKHR(*instance.getVkSurface());
    return details;
}

QueueFamilyIndices Device::getQueueFamilyIndices() { return indices; }

vk::raii::PhysicalDevice &Device::getPhysicalDevice() { return *physicalDevice; }

vk::raii::Device &Device::getLogicalDevice() { return *logicalDevice; }

vk::raii::Queue &Device::getPresentQueue() { return *presentQueue; }
