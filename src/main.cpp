#include "engine/Engine.hpp"

#include <engine/internal/Window.hpp>
#include <game/DemoGame.hpp>
#include <iostream>

int main() {
    Window window;
    VulkanContext vkCtx{};
    Engine engine(&window, &vkCtx);
    DemoGame game;

    try {
        window.init("volkanite", 2560, 1440);

        engine.setGame(&game);
        engine.init();
        engine.run();
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
