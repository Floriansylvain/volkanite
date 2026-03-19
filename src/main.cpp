#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>

const auto APPLICATION_NAME = "Vulkan M4 Setup";

SDL_Window *initSDL3() {
    if (SDL_Init(SDL_INIT_VIDEO) == false) {
        SDL_Log("Erreur SDL_Init : %s", SDL_GetError());
        throw std::runtime_error("Failed to initialize SDL");
    }

    SDL_Window* window = SDL_CreateWindow(APPLICATION_NAME, 800, 600, SDL_WINDOW_VULKAN);
    if (!window) {
        std::cerr << "Erreur création fenêtre : " << SDL_GetError() << std::endl;
        throw std::runtime_error("Failed to create window");
    }

    return window;
}

VkInstance initVulkan() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = APPLICATION_NAME;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    uint32_t extensionCount = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);

    std::vector<const char*> requiredExtensions;
    for (uint32_t i = 0; i < extensionCount; i++) {
        requiredExtensions.emplace_back(extensions[i]);
    }

    requiredExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

    createInfo.enabledExtensionCount = (uint32_t) requiredExtensions.size();
    createInfo.ppEnabledExtensionNames = requiredExtensions.data();

    createInfo.enabledLayerCount = 0;

    VkInstance instance;
    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("failed to create instance!");
    }

    return instance;
}

void cleanup(VkInstance* instance, SDL_Window* window) {
    vkDestroyInstance(*instance, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

int main(int argc, char* argv[]) {
    SDL_Window* window = initSDL3();
    VkInstance instance = initVulkan();

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }
    }

    cleanup(&instance, window);

    return 0;
}
