#include "core/Engine.hpp"

#include <core/Window.hpp>
#include <iostream>

int main() {
    Window window;
    VulkanContext vkCtx{};
    Engine engine(&window, &vkCtx);

    try {
        window.init("volkanite", 2560, 1440);
        engine.init();
        engine.run();
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
