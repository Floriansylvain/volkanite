#include "Window.hpp"
#include "Constants.hpp"
#include "Exceptions.hpp"
#include "SDL3/SDL_vulkan.h"

#include <iostream>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>

Window::Window() = default;

Window::~Window() {
    if (isWindowCreated) {
        SDL_DestroyWindow(SDL_Window);
    }

    if (isInitialized) {
        SDL_Quit();
    }
}

void Window::init(const char *title, const int width, const int height) {
    if (!isInitialized) {
        if (SDL_Init(SDL_INIT_VIDEO) == false) {
            throw EngineExceptions::Compatibility(std::string("Failed to initialize SDL : ") + SDL_GetError());
        }
        isInitialized = true;
    }

    if (!isWindowCreated) {
        SDL_Window = SDL_CreateWindow(title, width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!SDL_Window) {
            throw EngineExceptions::Compatibility(std::string("Failed to create window : ") + SDL_GetError());
        }
        isWindowCreated = true;
    }

    SDL_SetWindowRelativeMouseMode(SDL_Window, focusToggle);

    running = true;
}

SDL_Window *Window::getSDL_window() const { return SDL_Window; }

void Window::createSurface(const VkInstance instance, const VkAllocationCallbacks *allocator, VkSurfaceKHR *surface) const {
    if (!SDL_Vulkan_CreateSurface(SDL_Window, instance, allocator, surface)) {
        throw EngineExceptions::Compatibility(std::string("Failed to create vulkan surface : ") + SDL_GetError());
    }
}

std::vector<const char *> Window::getInstanceExtensions([[maybe_unused]] uint32_t *count) {
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

Window::Action Window::action_user_should_take(const SDL_Event *e) {
    if (e->type != SDL_EVENT_KEY_DOWN)
        return ACTION_NONE;

    if (e->key.scancode == SDL_SCANCODE_T) {
        return ACTION_WIREFRAME;
    }
    if (e->key.scancode == SDL_SCANCODE_ESCAPE) {
        return ACTION_BLUR;
    }
    return ACTION_NONE;
}

void Window::pollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            running = false;
        } else if ((event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_MINIMIZED) && onChange) {
            onChange();
        }

        const auto action = action_user_should_take(&event);

        if (onWireframeToggle && action == ACTION_WIREFRAME) {
            onWireframeToggle();
        } else if (action == ACTION_BLUR) {
            focusToggle = !focusToggle;
            SDL_SetWindowRelativeMouseMode(SDL_Window, focusToggle);     
        }
    }
}

void Window::waitEvents() {
    SDL_Event event;
    if (const bool success = SDL_WaitEvent(&event); !success) {
        throw EngineExceptions::Render(std::string("Failed to wait for SDL event : ") + SDL_GetError());
    }
    if (event.type == SDL_EVENT_QUIT) {
        running = false;
    } else if ((event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_MINIMIZED) && onChange) {
        onChange();
    }
}

bool Window::isMinimized() const { return SDL_GetWindowFlags(SDL_Window) & SDL_WINDOW_MINIMIZED; }

bool Window::isRunning() const { return running; }

void Window::setWindowTitle(const std::string &title) const { SDL_SetWindowTitle(SDL_Window, title.c_str()); }
