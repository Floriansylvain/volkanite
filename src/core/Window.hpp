#ifndef WINDOW_HPP
#define WINDOW_HPP

struct SDL_Window;

class Window {
  public:
    Window(const char *title, int width, int height);
    ~Window();

    SDL_Window *getSDL_window();

  private:
    SDL_Window *_SDL_Window = nullptr;
    void initSDL3(const char *title, int width, int height);
};

#endif
