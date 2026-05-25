#include "Engine.hpp"
#include "Constants.hpp"

#include <iostream>

Engine::Engine(Window *window) : window(*window) {};

Engine::~Engine() = default;

void Engine::setupDebugMessenger() {
    if constexpr (!enableValidationLayers)
        return;

    constexpr vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                                                  vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
    constexpr vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                                                                 vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                                                                 vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);

    vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{};
    debugUtilsMessengerCreateInfoEXT.messageSeverity = severityFlags;
    debugUtilsMessengerCreateInfoEXT.messageType = messageTypeFlags;
    debugUtilsMessengerCreateInfoEXT.pfnUserCallback = &debugCallback;

    debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
}

void Engine::createInstance() {
    vk::ApplicationInfo appInfo{};
    appInfo.pApplicationName = "Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "volkanite";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = vk::ApiVersion14;

    std::vector<char const *> requiredLayers;
    if (enableValidationLayers) {
        requiredLayers.assign(validationLayers.begin(), validationLayers.end());
    }

    auto layerProperties = context.enumerateInstanceLayerProperties();
    const auto unsupportedLayerIt = std::ranges::find_if(requiredLayers, [&layerProperties](auto const &requiredLayer) {
        return std::ranges::none_of(layerProperties, [requiredLayer](auto const &layerProperty) {
            return strcmp(layerProperty.layerName, requiredLayer) == 0;
        });
    });
    if (unsupportedLayerIt != requiredLayers.end()) {
        throw std::runtime_error("Required layer not supported : " + std::string(*unsupportedLayerIt));
    }

    uint32_t sdlExtensionsCount = 0;
    auto requiredExtensions = Window::getInstanceExtensions(&sdlExtensionsCount);

    auto extensionProperties = context.enumerateInstanceExtensionProperties();
    const auto unsupportedPropertyIt =
        std::ranges::find_if(requiredExtensions, [&extensionProperties](auto const &requiredExtension) {
            return std::ranges::none_of(extensionProperties, [requiredExtension](auto const &extensionProperty) {
                return strcmp(extensionProperty.extensionName, requiredExtension) == 0;
            });
        });
    if (unsupportedPropertyIt != requiredExtensions.end()) {
        throw std::runtime_error("Required extension not supported : " + std::string(*unsupportedPropertyIt));
    }

    vk::InstanceCreateInfo createInfo{};
    createInfo.pApplicationInfo = &appInfo;

#if defined(__APPLE__) || defined(__MACH__)
    createInfo.flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
#endif

    createInfo.enabledLayerCount = static_cast<uint32_t>(requiredLayers.size());
    createInfo.ppEnabledLayerNames = requiredLayers.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
    createInfo.ppEnabledExtensionNames = requiredExtensions.data();

    instance = vk::raii::Instance(context, createInfo);

    if (enableValidationLayers) {
        const auto extensions = context.enumerateInstanceExtensionProperties();
        std::cout << "available extensions:\n";
        for (const auto &extension : extensions) {
            std::cout << '\t' << extension.extensionName << '\n';
        }
    }

    isInitialized = true;
}

bool Engine::isDeviceSuitable(vk::raii::PhysicalDevice const &_physicalDevice) {
    bool supportsVulkan1_3 = _physicalDevice.getProperties().apiVersion >= vk::ApiVersion13;

    auto queueFamilies = _physicalDevice.getQueueFamilyProperties();
    bool supportsGraphics =
        std::ranges::any_of(queueFamilies, [](auto const &qfp) { return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics); });

    auto availableDeviceExtensions = _physicalDevice.enumerateDeviceExtensionProperties();
    bool supportsAllRequiredExtensions =
        std::ranges::all_of(requiredDeviceExtension, [&availableDeviceExtensions](auto const &requiredDeviceExtension) {
            return std::ranges::any_of(availableDeviceExtensions,
                                       [requiredDeviceExtension](auto const &availableDeviceExtension) {
                                           return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0;
                                       });
        });

    auto features = _physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features,
                                                 vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
    bool supportsRequiredFeatures = features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
                                    features.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

    return supportsVulkan1_3 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
}

void Engine::pickPhysicalDevice() {
    std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();
    auto const devIter =
        std::ranges::find_if(physicalDevices, [&](auto const &_physicalDevice) { return isDeviceSuitable(_physicalDevice); });
    if (devIter == physicalDevices.end()) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
    physicalDevice = *devIter;
}

void Engine::createLogicalDevice() {
    std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
    const auto graphicsQueueFamilyProperty = std::ranges::find_if(queueFamilyProperties, [](auto const &qfp) {
        return (qfp.queueFlags & vk::QueueFlagBits::eGraphics) != static_cast<vk::QueueFlags>(0);
    });
    const auto graphicsIndex = static_cast<uint32_t>(std::distance(queueFamilyProperties.begin(), graphicsQueueFamilyProperty));

    float queuePriority = 0.5f;
    vk::DeviceQueueCreateInfo deviceQueueCreateInfo{};
    deviceQueueCreateInfo.queueFamilyIndex = graphicsIndex;
    deviceQueueCreateInfo.queueCount = 1;
    deviceQueueCreateInfo.pQueuePriorities = &queuePriority;

    vk::PhysicalDeviceFeatures2 features2{};
    vk::PhysicalDeviceVulkan13Features features13{};
    features13.dynamicRendering = true;
    vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT extendedFeatures{};
    extendedFeatures.extendedDynamicState = true;
    vk::StructureChain featureChain(features2, features13, extendedFeatures);

    std::vector _requiredDeviceExtension = {vk::KHRSwapchainExtensionName};

    vk::DeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>();
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &deviceQueueCreateInfo;
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(_requiredDeviceExtension.size());
    deviceCreateInfo.ppEnabledExtensionNames = _requiredDeviceExtension.data();

    device = vk::raii::Device(physicalDevice, deviceCreateInfo);
    graphicsQueue = vk::raii::Queue(device, graphicsIndex, 0);
}

void Engine::init() {
    createInstance();
    setupDebugMessenger();
    pickPhysicalDevice();
    createLogicalDevice();
}

void Engine::run() const {
    if (!isInitialized) {
        throw std::runtime_error("Failed to run Engine : Engine is not initialized");
    }
    if (!window.isRunning()) {
        throw std::runtime_error("Failed to run Engine : Window is not running");
    }

    while (window.isRunning()) {
        window.pollEvents();
    }
}

void Engine::cleanup() {}
