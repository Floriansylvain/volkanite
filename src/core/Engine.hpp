#ifndef ENGINE_HPP
#define ENGINE_HPP

#pragma once
#include "Camera.hpp"
#include "InstanceRenderer.hpp"
#include "Mesh.hpp"
#include "OcclusionCuller.hpp"
#include "RenderObject.hpp"
#include "SwapChainHandler.hpp"
#include "TextRenderer.hpp"
#include "VulkanContext.hpp"
#include <chrono>
#include <glm/glm.hpp>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

class Engine {
  public:
    explicit Engine(Window *_window, VulkanContext *_vkCtx);
    ~Engine();

    void init();
    void run();
    void cleanup();

  private:
    constexpr static float DEBUG_FONT_SIZE = 38.f;
    constexpr static int MAX_FRAMES_IN_FLIGHT = 2;
    constexpr static int MAX_TEXTURES = 16;

    Window &window;
    VulkanContext &vkCtx;
    SwapChainHandler swapChainHandler;
    Camera camera;
    InstanceRenderer instanceRenderer;
    TextRenderer textRenderer;
    OcclusionCuller occlusionCuller;

    std::vector<std::string> debugLines;
    float frameTimeAccumulator = 0.0f;
    uint32_t frameCountAccumulator = 0;
    uint32_t drawCallCount = 0;
    uint64_t vertexCount = 0;

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

    vk::raii::DescriptorPool descriptorPool = nullptr;

    std::vector<vk::raii::Buffer> cameraUniformBuffers;
    std::vector<vk::raii::DeviceMemory> cameraUniformBuffersMemory;
    std::vector<void *> cameraUniformBuffersMapped;

    std::unordered_map<std::shared_ptr<Texture>, std::vector<vk::raii::DescriptorSet>> textureDescriptorSets;

    std::vector<RenderObject> renderObjects;
    std::unordered_map<std::string, std::shared_ptr<Texture>> textureCache;

    bool isInitialized = false;
    bool framebufferResized = false;
    bool isWireframe = false;
    // TODO: replace by window action callback system
    bool showCullingDebug = false;

    std::chrono::high_resolution_clock::time_point engineStartTime;

    void createGraphicsPipeline();
    void createCommandPool();
    void createCommandBuffers();
    struct TransitionImageLayoutCommand {
        vk::Image image;
        vk::ImageLayout old_layout;
        vk::ImageLayout new_layout;
        vk::AccessFlags2 src_access_mask;
        vk::AccessFlags2 dst_access_mask;
        vk::PipelineStageFlags2 src_stage_mask;
        vk::PipelineStageFlags2 dst_stage_mask;
        vk::ImageAspectFlags image_aspect_flags;
    };
    struct BufferBarrierCommand {
        vk::Buffer buffer;
        vk::DeviceSize offset;
        vk::DeviceSize size;
        vk::AccessFlags2 src_access_mask;
        vk::AccessFlags2 dst_access_mask;
        vk::PipelineStageFlags2 src_stage_mask;
        vk::PipelineStageFlags2 dst_stage_mask;
    };
    void transition_image_layouts(const std::vector<TransitionImageLayoutCommand> &commands) const;
    void buffer_barriers(const std::vector<BufferBarrierCommand> &commands) const;
    void recordCommandBuffer(uint32_t imageIndex);
    void createSyncObjects();
    void drawFrame();

    struct UniformBufferObject {
        glm::mat4 view;
        glm::mat4 proj;
    };
    struct ModelPushConstant {
        glm::mat4 model;
    };
    void createDescriptorSetLayout();
    void createCameraUniformBuffer();
    void registerTexture(const std::shared_ptr<Texture> &texture);
    void updateUniformBuffer(uint32_t currentImage) const;
    void createDescriptorPool();
    void addRenderObject(RenderObject object);
    void updateInstanceBuffers(uint32_t currentImage);

    struct FBXModel {
        std::vector<std::shared_ptr<Mesh>> meshes;
        std::vector<std::shared_ptr<Texture>> textures;
    };
    FBXModel createFBXModel(const std::string &fbxPath, const std::string &fileExtension);
    void placeFBXModel(const FBXModel &model, const glm::vec3 &position, bool instanced = false);

    void recreateSwapChain();
    void createOcclusionCuller();

    void update();
};

#endif
