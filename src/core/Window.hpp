#ifndef WINDOW_HPP
#define WINDOW_HPP

#include "SDL3/SDL.h"
#include <cstdint>
#include <functional>

class Window {
  public:
    Window();
    ~Window();
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    SDL_Window *getSDL_window() const;
    const char *const *getInstanceExtensions(uint32_t *count) const;
    bool getSizeInPixels(int *w, int *h) const;

    void initSDL3(const char *title, int width, int height);
    bool isRunning() const;
    void pollEvents();
    void setChangeCallback(std::function<void(int, int)> callback) { onChange = callback; }

  private:
    SDL_Window *_SDL_Window = nullptr;

    bool running = false;
    bool isInitialized = false;
    bool isWindowCreated = false;
    std::function<void(int, int)> onChange;
};

#endif
