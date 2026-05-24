#include "Engine.hpp"

#include <iostream>

Engine::Engine(Window *window) : window(*window) {};

Engine::~Engine() = default;

void Engine::createInstance() {
    vk::ApplicationInfo appInfo{};
    appInfo.pApplicationName = "Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "volkanite";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = vk::ApiVersion14;

    uint32_t sdlExtensionsCount = 0;
    auto sdlExtensions = Window::getInstanceExtensions(&sdlExtensionsCount);

    auto extensionProperties = context.enumerateInstanceExtensionProperties();
    for (uint32_t i = 0; i < sdlExtensionsCount; ++i) {
        if (std::ranges::none_of(extensionProperties, [sdlExtension = sdlExtensions[i]](auto const &extensionProperty) {
                return strcmp(extensionProperty.extensionName, sdlExtension) == 0;
            })) {
            throw std::runtime_error("Required SDL extension not supported: " + std::string(sdlExtensions[i]));
        }
    }

    vk::InstanceCreateInfo createInfo{};
#if defined(__APPLE__) || defined(__MACH__)
    createInfo.flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
#endif
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = sdlExtensionsCount;
    createInfo.ppEnabledExtensionNames = sdlExtensions;

    instance = vk::raii::Instance(context, createInfo);

    const auto extensions = context.enumerateInstanceExtensionProperties();
    std::cout << "available extensions:\n";

    for (const auto &extension : extensions) {
        std::cout << '\t' << extension.extensionName << '\n';
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
