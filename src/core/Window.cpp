#include "Window.hpp"
#include "SDL3/SDL.h"
#include "SDL3/SDL_vulkan.h"

#include <iostream>

Window::Window(const char *title, int width, int height) : running(true) { initSDL3(title, width, height); };

Window::~Window() {
    SDL_DestroyWindow(_SDL_Window);
    SDL_Quit();
};

void Window::initSDL3(const char *title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) == false) {
        SDL_Log("Erreur SDL_Init : %s", SDL_GetError());
        throw std::runtime_error("Failed to initialize SDL");
    }

    _SDL_Window = SDL_CreateWindow(title, width, height, SDL_WINDOW_VULKAN);
    if (!_SDL_Window) {
        std::cerr << "Erreur création fenêtre : " << SDL_GetError() << std::endl;
        throw std::runtime_error("Failed to create window");
    }
}

SDL_Window *Window::getSDL_window() { return _SDL_Window; };

const char *const *Window::getInstanceExtensions(uint32_t *count) { return SDL_Vulkan_GetInstanceExtensions(count); };

bool Window::getSizeInPixels(int *w, int *h) { return SDL_GetWindowSizeInPixels(_SDL_Window, w, h); }

void Window::pollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT)
            running = false;
    }
}

bool Window::isRunning() { return running; }
