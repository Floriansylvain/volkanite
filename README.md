# volkanite

This is my playground to learn:

1. c++
2. graphics programming
3. vulkan api
4. linear algebra

Refac TODO:

| Class     | Responsibility     | Primary Members                                   |
| --------- | ------------------ | ------------------------------------------------- |
| Window    | OS Abstraction     | `SDL_Window`, `Input events`                      |
| Instance  | Vulkan Setup       | `vk::raii::Instance`, `Surface`, `DebugMessenger` |
| Device    | Hardware Interface | `PhysicalDevice`, `Device`, `Queues`              |
| Swapchain | Image Buffering    | `SwapchainKHR`, `ImageViews`                      |
| Pipeline  | The "GPU Program"  | `PipelineLayout`, `ShaderModules`, `Pipeline`     |
