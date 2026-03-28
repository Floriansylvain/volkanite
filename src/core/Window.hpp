#ifndef WINDOW_HPP
#define WINDOW_HPP

struct SDL_Window;

#include <cstdint>
#include <functional>

// OS Abstraction
class Window {
  public:
    Window(const char *title, int width, int height);
    ~Window();
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    SDL_Window *getSDL_window();
    const char *const *getInstanceExtensions(uint32_t *count);
    bool getSizeInPixels(int *w, int *h);

    bool isRunning();
    void pollEvents();
    void setChangeCallback(std::function<void(int, int)> callback) { onChange = callback; }
    void waitEvents();

  private:
    SDL_Window *_SDL_Window = nullptr;

    bool running;
    std::function<void(int, int)> onChange;

    void initSDL3(const char *title, int width, int height);
};

#endif
