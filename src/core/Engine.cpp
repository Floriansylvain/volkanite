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

void Engine::init() { createInstance(); }

void Engine::run() const {
    if (!isInitialized) {
        throw std::runtime_error("Engine is not initialized");
    }
    if (!window.isRunning()) {
        throw std::runtime_error("Failed to run Engine : Window is not running");
    }

    while (window.isRunning()) {
        window.pollEvents();
    }
}

void Engine::cleanup() {}
