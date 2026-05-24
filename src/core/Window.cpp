#include "Window.hpp"
#include "SDL3/SDL.h"
#include "SDL3/SDL_vulkan.h"

#include <iostream>

Window::Window() {};

Window::~Window() {
    if (isWindowCreated) {
        SDL_DestroyWindow(_SDL_Window);
    }

    if (isInitialized) {
        SDL_Quit();
    }
};

void Window::initSDL3(const char *title, int width, int height) {
    if (!isInitialized) {
        if (SDL_Init(SDL_INIT_VIDEO) == false) {
            SDL_Log("SDL_Init error : %s", SDL_GetError());
            throw std::runtime_error("Failed to initialize SDL");
        }
        isInitialized = true;
    }

    if (!isWindowCreated) {
        _SDL_Window = SDL_CreateWindow(title, width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!_SDL_Window) {
            std::cerr << "SDL_CreateWindow error : " << SDL_GetError() << std::endl;
            throw std::runtime_error("Failed to create window");
        }
        isWindowCreated = true;
    }

    running = true;
}

SDL_Window *Window::getSDL_window() const { return _SDL_Window; }

const char *const *Window::getInstanceExtensions(uint32_t *count) const { return SDL_Vulkan_GetInstanceExtensions(count); }

bool Window::getSizeInPixels(int *w, int *h) const { return SDL_GetWindowSizeInPixels(_SDL_Window, w, h); }

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
