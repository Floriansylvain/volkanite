#ifndef RENDERER_HPP
#define RENDERER_HPP

#include "Device.hpp"
#include "Pipeline.hpp"
#include "SwapChain.hpp"
#include <optional>
#include <vulkan/vulkan_raii.hpp>

// Frame orchestration
class Renderer {
  public:
    Renderer(Device &device, SwapChain &swapChain, Pipeline &pipeline);
    ~Renderer();

    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    void render();
    void setFramebufferResized();

  private:
    const int MAX_FRAMES_IN_FLIGHT = 2;

    Device &device;
    SwapChain &swapChain;
    Pipeline &pipeline;

    struct SyncObjects {
        std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
        std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
        std::vector<vk::raii::Fence> inFlightFences;
    };

    std::optional<vk::raii::CommandPool> commandPool;
    std::vector<vk::raii::CommandBuffer> commandBuffers;
    SyncObjects syncObjects;
    uint32_t frameIndex;
    bool framebufferResized;

    void initCommandPool();
    void initCommandBuffer();
    void initSyncObjects();

    void recordCommandBuffer(uint32_t imageIndex);
    void transition_image_layout(uint32_t imageIndex, vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                                 vk::AccessFlags2 srcAccessMask, vk::AccessFlags2 dstAccessMask,
                                 vk::PipelineStageFlags2 srcStageMask, vk::PipelineStageFlags2 dstStageMask,
                                 const vk::raii::CommandBuffer &commandBuffer);
};

#endif
