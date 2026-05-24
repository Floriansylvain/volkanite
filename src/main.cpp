#include <core/Window.hpp>

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

int main() {
    Window window;
    window.initSDL3("volkanite", 800, 600);

    while (window.isRunning()) {
        window.pollEvents();
    }

    return 0;
}
