#include "Instance.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan_raii.hpp>

#include <iostream>

const std::vector<const char *> validationLayers = {"VK_LAYER_KHRONOS_validation"};

Instance::Instance(Window &window, bool isDebug)
    : window(window), context(), instance(context, {}), isDebug(isDebug), surface(instance, nullptr) {
    initVkInstance();
    initDebugMessenger();
    initVkSurface();
}

Instance::~Instance() { DestroyDebugUtilsMessengerEXT(nullptr); }

bool Instance::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    return std::all_of(validationLayers.begin(), validationLayers.end(), [&availableLayers](const char *layerName) {
        return std::ranges::any_of(availableLayers, [layerName](const auto &layerProperties) {
            return strcmp(layerName, layerProperties.layerName) == 0;
        });
    });
}

VKAPI_ATTR VkBool32 VKAPI_CALL Instance::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                       VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                       const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                                       void *pUserData) {
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    }
    return VK_FALSE;
}

void Instance::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
}

VkResult Instance::CreateDebugUtilsMessengerEXT(const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
                                                const VkAllocationCallbacks *pAllocator) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(*instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(*instance, pCreateInfo, pAllocator, &debugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void Instance::DestroyDebugUtilsMessengerEXT(const VkAllocationCallbacks *pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(*instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(*instance, debugMessenger, pAllocator);
    }
}

void Instance::initDebugMessenger() {
    if (!isDebug)
        return;
    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateDebugMessengerCreateInfo(createInfo);

    if (CreateDebugUtilsMessengerEXT(&createInfo, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger!");
    }
}

void Instance::initVkInstance() {
    if (isDebug && !checkValidationLayerSupport()) {
        throw std::runtime_error("validation layers requested, but not available!");
    }

    vk::ApplicationInfo appInfo;
    appInfo.pApplicationName = "volkanite";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_4;

    uint32_t extensionCount = 0;
    const char *const *extensions = window.getInstanceExtensions(&extensionCount);

    std::vector<const char *> requiredExtensions;
    for (uint32_t i = 0; i < extensionCount; i++) {
        requiredExtensions.emplace_back(extensions[i]);
    }
    requiredExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    requiredExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    if (isDebug) {
        requiredExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    vk::InstanceCreateInfo createInfo;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
    createInfo.ppEnabledExtensionNames = requiredExtensions.data();
    createInfo.flags = vk::InstanceCreateFlags();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (isDebug) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    createInfo.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;

    instance = vk::raii::Instance(context, createInfo);
}

void Instance::initVkSurface() {
    VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window.getSDL_window(), *instance, NULL, &rawSurface)) {
        throw std::runtime_error("Failed to create window surface: " + std::string(SDL_GetError()));
    }
    surface = vk::raii::SurfaceKHR(instance, rawSurface);
}

vk::raii::Instance *Instance::getVkInstance() { return &instance; }

vk::raii::SurfaceKHR *Instance::getVkSurface() { return &surface; }

std::vector<vk::raii::PhysicalDevice> Instance::getPhysicalDevices() { return instance.enumeratePhysicalDevices(); }
