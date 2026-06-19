#pragma once
#include "VulkanContext.hpp"
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "Window.hpp"
#include <glm/glm.hpp>

#ifndef ENGINE_HPP
#define ENGINE_HPP

class Engine {
  public:
    explicit Engine(Window *_window, VulkanContext *_vkCtx);
    ~Engine();

    void init();
    void run();
    void cleanup();

  private:
    constexpr static int MAX_FRAMES_IN_FLIGHT = 2;

    Window &window;
    VulkanContext &vkCtx;

    vk::raii::SwapchainKHR swapChain = nullptr;
    std::vector<vk::Image> swapChainImages;
    vk::SurfaceFormatKHR swapChainSurfaceFormat;
    vk::Extent2D swapChainExtent;
    std::vector<vk::raii::ImageView> swapChainImageViews;
    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline solidGraphicsPipeline = nullptr;
    vk::raii::Pipeline wireframeGraphicsPipeline = nullptr;
    vk::raii::CommandPool commandPool = nullptr;
    std::vector<vk::raii::CommandBuffer> commandBuffers;
    std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
    std::vector<vk::raii::Fence> inFlightFences;
    uint32_t frameIndex = 0;
    vk::raii::Buffer unifiedBuffer = nullptr;
    vk::raii::DeviceMemory unifiedBufferMemory = nullptr;
    std::vector<vk::raii::Buffer> uniformBuffers;
    std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
    std::vector<void *> uniformBuffersMapped;
    vk::raii::DescriptorPool descriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> descriptorSets;
    vk::raii::Image textureImage = nullptr;
    vk::raii::DeviceMemory textureImageMemory = nullptr;
    vk::raii::ImageView textureImageView = nullptr;
    vk::raii::Sampler textureSampler = nullptr;
    vk::raii::Image depthImage = nullptr;
    vk::raii::DeviceMemory depthImageMemory = nullptr;
    vk::raii::ImageView depthImageView = nullptr;

    bool isInitialized = false;
    bool framebufferResized = false;
    bool isWireframe = false;

    static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats);
    static vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const &availablePresentModes);
    [[nodiscard]] vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const &capabilities) const;
    static uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const &surfaceCapabilities);
    void createSwapChain();
    void createImageViews();

    static std::vector<char> readFile(const std::string &filename);
    [[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char> &code) const;
    void createGraphicsPipeline();
    void createCommandPool();
    void createCommandBuffers();
    struct transitionImageLayoutCommand {
        vk::Image image;
        vk::ImageLayout old_layout;
        vk::ImageLayout new_layout;
        vk::AccessFlags2 src_access_mask;
        vk::AccessFlags2 dst_access_mask;
        vk::PipelineStageFlags2 src_stage_mask;
        vk::PipelineStageFlags2 dst_stage_mask;
        vk::ImageAspectFlags image_aspect_flags;
    };
    void transition_image_layout(transitionImageLayoutCommand const &command) const;
    void recordCommandBuffer(uint32_t imageIndex) const;
    void createSyncObjects();
    void drawFrame();
    void recreateSwapChain();
    void cleanupSwapChain();

    struct Vertex {
        glm::vec3 pos;
        glm::vec3 color;
        glm::vec2 texCoord;

        static vk::VertexInputBindingDescription getBindingDescription();
        static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions();
    };
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

    [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;
    [[nodiscard]] std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>
    createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties) const;
    [[nodiscard]] vk::raii::CommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(vk::raii::CommandBuffer &&commandBuffer) const;

    void copyBuffer(const vk::raii::Buffer &srcBuffer, const vk::raii::Buffer &dstBuffer, vk::DeviceSize size) const;
    void createGeometryBuffers();

    struct UniformBufferObject {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
    };
    void createDescriptorSetLayout();
    void createUniformBuffers();
    void updateUniformBuffer(uint32_t currentImage) const;
    void createDescriptorPool();
    void createDescriptorSets();
    [[nodiscard]] std::pair<vk::raii::Image, vk::raii::DeviceMemory> createImage(uint32_t width, uint32_t height,
                                                                                 vk::Format format, vk::ImageTiling tiling,
                                                                                 vk::ImageUsageFlags usage,
                                                                                 vk::MemoryPropertyFlags properties) const;
    void createTextureImage();
    static void transitionImageLayout(const vk::raii::CommandBuffer &commandBuffer, const vk::raii::Image &image,
                                      vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
    static void copyBufferToImage(const vk::raii::CommandBuffer &commandBuffer, const vk::raii::Buffer &buffer,
                                  const vk::raii::Image &image, uint32_t width, uint32_t height);
    [[nodiscard]] vk::raii::ImageView createImageView(vk::Image const &image, vk::Format format,
                                                      vk::ImageAspectFlags aspectFlags) const;
    void createTextureImageView();
    void createTextureSampler();
    [[nodiscard]] vk::Format findSupportedFormat(const std::vector<vk::Format> &candidates, vk::ImageTiling tiling,
                                                 vk::FormatFeatureFlags features) const;
    [[nodiscard]] vk::Format findDepthFormat() const;
    void createDepthResources();
    void generateCubeData(float size);
};

#endif
