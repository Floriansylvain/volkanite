#ifndef WINDOW_HPP
#define WINDOW_HPP

#include "SDL3/SDL.h"
#include <cstdint>
#include <functional>

class Window {
  public:
    Window();
    ~Window();

    [[nodiscard]] SDL_Window *getSDL_window() const;
    const char *const *getInstanceExtensions(uint32_t *count) const;
    bool getSizeInPixels(int *w, int *h) const;

    void init(const char *title, int width, int height);
    [[nodiscard]] bool isRunning() const;
    void pollEvents();
    void setChangeCallback(const std::function<void(int, int)> &callback) { onChange = callback; }

  private:
    SDL_Window *SDL_Window = nullptr;

    bool running = false;
    bool isInitialized = false;
    bool isWindowCreated = false;
    std::function<void(int, int)> onChange;
};

#endif
