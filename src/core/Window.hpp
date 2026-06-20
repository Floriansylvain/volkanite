#ifndef WINDOW_HPP
#define WINDOW_HPP

#include "SDL3/SDL.h"
#include <functional>
#include <vulkan/vulkan_core.h>
#include <string>

class Window {
  public:
    Window();
    ~Window();

    [[nodiscard]] SDL_Window *getSDL_window() const;
    void createSurface(VkInstance instance, const VkAllocationCallbacks *allocator, VkSurfaceKHR *surface) const;
    static std::vector<const char *> getInstanceExtensions(uint32_t *count);
    bool getSizeInPixels(int *w, int *h) const;

    void init(const char *title, int width, int height);
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] bool isMinimized() const;
    void pollEvents();
    void waitEvents();
    void setChangeCallback(const std::function<void()> &callback) { onChange = callback; }
    void setWireframeCallback(const std::function<void()> &callback) { onWireframeToggle = callback; }

    void setWindowTitle(const std::string &title) const;

  private:
    SDL_Window *SDL_Window = nullptr;

    enum Action { ACTION_NONE, ACTION_WIREFRAME, ACTION_BLUR };
    static Action action_user_should_take(const SDL_Event *e);

    bool running = false;
    bool isInitialized = false;
    bool isWindowCreated = false;
    bool focusToggle = true;
    std::function<void()> onChange;
    std::function<void()> onWireframeToggle;
};

#endif
