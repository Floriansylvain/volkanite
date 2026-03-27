#ifndef WINDOW_HPP
#define WINDOW_HPP

struct SDL_Window;

#include <cstdint>

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

  private:
    SDL_Window *_SDL_Window = nullptr;
    void initSDL3(const char *title, int width, int height);
};

#endif
