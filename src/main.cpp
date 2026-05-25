#include "core/Engine.hpp"

#include <core/Window.hpp>
#include <iostream>
#include <stdexcept>

int main() {
    Window window;
    Engine engine(&window);

    try {
        window.init("volkanite", 800, 600);
        engine.init();
        engine.run();
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
