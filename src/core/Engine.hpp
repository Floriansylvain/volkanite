#pragma once
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "Window.hpp"

#ifndef ENGINE_HPP
#define ENGINE_HPP

class Engine {
  public:
    Engine(Window *window);
    ~Engine();

    void init();
    void run() const;
    static void cleanup();

  private:
    Window &window;

    vk::raii::Context context;
    vk::raii::Instance instance = nullptr;

    bool isInitialized = false;

    void createInstance();
};

#endif
