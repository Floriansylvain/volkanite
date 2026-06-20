#include "VulkanContext.hpp"
#include "Constants.hpp"
#include "Exceptions.hpp"
#include "Window.hpp"

void VulkanContext::setupDebugMessenger() {
    if constexpr (!enableValidationLayers)
        return;

    using enum vk::DebugUtilsMessageSeverityFlagBitsEXT;
    constexpr vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(eWarning | eError);

    using enum vk::DebugUtilsMessageTypeFlagBitsEXT;
    constexpr vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(eGeneral | ePerformance | eValidation);

    vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{};
    debugUtilsMessengerCreateInfoEXT.messageSeverity = severityFlags;
    debugUtilsMessengerCreateInfoEXT.messageType = messageTypeFlags;
    debugUtilsMessengerCreateInfoEXT.pfnUserCallback = &debugCallback;

    debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
}

void VulkanContext::createInstance() {
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

    if (const auto unsupportedLayerIt =
            std::ranges::find_if(requiredLayers,
                                 [&layerProperties](auto const &requiredLayer) {
                                     return std::ranges::none_of(layerProperties, [requiredLayer](auto const &layerProperty) {
                                         return strcmp(layerProperty.layerName, requiredLayer) == 0;
                                     });
                                 });
        unsupportedLayerIt != requiredLayers.end()) {
        throw EngineExceptions::Compatibility("Required layer not supported : " + std::string(*unsupportedLayerIt));
    }

    uint32_t sdlExtensionsCount = 0;
    auto requiredExtensions = Window::getInstanceExtensions(&sdlExtensionsCount);
    auto extensionProperties = context.enumerateInstanceExtensionProperties();

    if (const auto unsupportedPropertyIt = std::ranges::find_if(
            requiredExtensions,
            [&extensionProperties](auto const &requiredExtension) {
                return std::ranges::none_of(extensionProperties, [requiredExtension](auto const &extensionProperty) {
                    return strcmp(extensionProperty.extensionName, requiredExtension) == 0;
                });
            });
        unsupportedPropertyIt != requiredExtensions.end()) {
        throw EngineExceptions::Compatibility("Required extension not supported : " + std::string(*unsupportedPropertyIt));
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
}

bool VulkanContext::isDeviceSuitable(vk::raii::PhysicalDevice const &_physicalDevice) {
    bool supportsVulkan1_3 = _physicalDevice.getProperties().apiVersion >= vk::ApiVersion13;

    auto queueFamilies = _physicalDevice.getQueueFamilyProperties();
    bool supportsGraphics =
        std::ranges::any_of(queueFamilies, [](auto const &qfp) { return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics); });

    auto availableDeviceExtensions = _physicalDevice.enumerateDeviceExtensionProperties();
    bool supportsAllRequiredExtensions =
        std::ranges::all_of(requiredDeviceExtensions, [&availableDeviceExtensions](auto const &requiredDeviceExtension) {
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

void VulkanContext::pickPhysicalDevice() {
    std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();
    auto const devIter =
        std::ranges::find_if(physicalDevices, [&](auto const &_physicalDevice) { return isDeviceSuitable(_physicalDevice); });
    if (devIter == physicalDevices.end()) {
        throw EngineExceptions::Compatibility("Could not find a suitable GPU");
    }
    physicalDevice = *devIter;
}

void VulkanContext::createLogicalDevice() {
    std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
    for (uint32_t qfpIndex = 0; std::cmp_less(qfpIndex, queueFamilyProperties.size()); qfpIndex++) {
        if (queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics &&
            physicalDevice.getSurfaceSupportKHR(qfpIndex, *surface)) {
            queueIndex = qfpIndex;
            break;
        }
    }
    if (std::cmp_equal(queueIndex, ~0)) {
        throw EngineExceptions::Compatibility("Could not find a queue for graphics and present");
    }

    vk::PhysicalDeviceFeatures2 features2{};
    features2.features.fillModeNonSolid = true;
    features2.features.samplerAnisotropy = true;

    vk::PhysicalDeviceVulkan11Features features11{};
    features11.shaderDrawParameters = true;

    vk::PhysicalDeviceVulkan13Features features13{};
    features13.dynamicRendering = true;
    features13.synchronization2 = true;

    vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT extendedFeatures{};
    extendedFeatures.extendedDynamicState = true;

    vk::StructureChain featureChain(features2, features11, features13, extendedFeatures);

    std::vector _requiredDeviceExtension = {vk::KHRSwapchainExtensionName};
    for (auto const availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();
         const auto &ext : availableExtensions) {
        if (std::strcmp(ext.extensionName, "VK_KHR_portability_subset") == 0) {
            _requiredDeviceExtension.push_back("VK_KHR_portability_subset");
            break;
        }
    }

    float queuePriority = 0.5f;
    vk::DeviceQueueCreateInfo deviceQueueCreateInfo{};
    deviceQueueCreateInfo.queueFamilyIndex = queueIndex;
    deviceQueueCreateInfo.queueCount = 1;
    deviceQueueCreateInfo.pQueuePriorities = &queuePriority;

    vk::DeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>();
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &deviceQueueCreateInfo;
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(_requiredDeviceExtension.size());
    deviceCreateInfo.ppEnabledExtensionNames = _requiredDeviceExtension.data();

    device = vk::raii::Device(physicalDevice, deviceCreateInfo);
    queue = vk::raii::Queue(device, queueIndex, 0);
}

void VulkanContext::createSurface(const Window &window) {
    VkSurfaceKHR _surface;
    window.createSurface(*instance, nullptr, &_surface);
    surface = vk::raii::SurfaceKHR(instance, _surface);
}

void VulkanContext::init(const Window &window) {
    createInstance();
    setupDebugMessenger();
    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();
}
