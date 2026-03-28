#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_beta.h>
#include <vulkan/vulkan_raii.hpp>

#include <core/Device.hpp>
#include <core/Instance.hpp>
#include <core/Pipeline.hpp>
#include <core/Renderer.hpp>
#include <core/SwapChain.hpp>
#include <core/Window.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <ranges>
#include <set>
#include <vector>

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

    while (window.isRunning()) {
        window.pollEvents();
        renderer.render();
    };

    device.getLogicalDevice().waitIdle();

    return 0;
}
