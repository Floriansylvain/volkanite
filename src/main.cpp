#include <core/Device.hpp>
#include <core/Instance.hpp>
#include <core/Pipeline.hpp>
#include <core/Renderer.hpp>
#include <core/SwapChain.hpp>
#include <core/Window.hpp>

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

int main() {
    Window window("volkanite", 800, 600);
    Instance instance(window, enableValidationLayers);
    Device device(instance);
    SwapChain swapChain(window, instance, device);
    Pipeline pipeline(device, swapChain);
    Renderer renderer(device, swapChain, pipeline);

    window.setChangeCallback([&](int w, int h) { renderer.setFramebufferResized(); });

    while (window.isRunning()) {
        window.pollEvents();
        renderer.render();
    };

    device.getLogicalDevice().waitIdle();

    return 0;
}
