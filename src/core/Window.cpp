#include "Window.hpp"
#include "Constants.hpp"
#include "SDL3/SDL_vulkan.h"

#include <iostream>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>

Window::Window() {};

Window::~Window() {
    if (isWindowCreated) {
        SDL_DestroyWindow(SDL_Window);
    }

    if (isInitialized) {
        SDL_Quit();
    }
};

void Window::init(const char *title, const int width, const int height) {
    if (!isInitialized) {
        if (SDL_Init(SDL_INIT_VIDEO) == false) {
            SDL_Log("SDL_Init error : %s", SDL_GetError());
            throw std::runtime_error("Failed to initialize SDL");
        }
        isInitialized = true;
    }

    if (!isWindowCreated) {
        SDL_Window = SDL_CreateWindow(title, width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!SDL_Window) {
            throw std::runtime_error(std::string("Failed to create window : ") + SDL_GetError());
        }
        isWindowCreated = true;
    }

    running = true;
}

SDL_Window *Window::getSDL_window() const { return SDL_Window; }

void Window::createSurface(const VkInstance instance, const VkAllocationCallbacks *allocator, VkSurfaceKHR *surface) const {
    if (!SDL_Vulkan_CreateSurface(SDL_Window, instance, allocator, surface)) {
        throw std::runtime_error(std::string("Failed to create vulkan surface : ") + SDL_GetError());
    }
}

std::vector<const char *> Window::getInstanceExtensions(uint32_t *count) {
    uint32_t sdlCount = 0;
    const auto sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlCount);

    static std::vector<const char *> extendedExtensions;
    extendedExtensions.clear();

    for (uint32_t i = 0; i < sdlCount; ++i) {
        extendedExtensions.push_back(sdlExtensions[i]);
    }

    if (enableValidationLayers) {
        extendedExtensions.push_back(vk::EXTDebugUtilsExtensionName);
    }

#if defined(__APPLE__) || defined(__MACH__)
    extendedExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

    return extendedExtensions;
}

bool Window::getSizeInPixels(int *w, int *h) const { return SDL_GetWindowSizeInPixels(SDL_Window, w, h); }

void Window::pollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            running = false;
        } else if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_MINIMIZED) {
            if (onChange) {
                onChange(event.window.data1, event.window.data2);
            }
        }
    }
}

bool Window::isRunning() const { return running; }
