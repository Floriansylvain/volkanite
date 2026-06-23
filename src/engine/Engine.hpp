#ifndef ENGINE_HPP
#define ENGINE_HPP

#pragma once
#include "Camera.hpp"
#include "Debug.hpp"
#include "Game.hpp"
#include "InstanceRenderer.hpp"
#include "Mesh.hpp"
#include "OcclusionCuller.hpp"
#include "RenderObject.hpp"
#include "SwapChainHandler.hpp"
#include "TextRenderer.hpp"

class Engine {
  public:
    explicit Engine(Window *_window, VulkanContext *_vkCtx);
    ~Engine();

    void init();
    void run();
    void cleanup();

    void setGame(Game *_game) { game = _game; }

    [[nodiscard]] Camera &getCamera() { return camera; }

    RenderObjectHandle addRenderObject(RenderObject object);
    [[nodiscard]] RenderObject &getRenderObject(RenderObjectHandle handle);

    struct FBXModel {
        std::vector<std::shared_ptr<Mesh>> meshes;
        std::vector<Material> materials;
    };
    FBXModel createFBXModel(const std::string &fbxPath, const std::string &fileExtension);
    void placeFBXModel(const FBXModel &model, const glm::vec3 &position);

    [[nodiscard]] std::shared_ptr<Mesh> createCubeMesh(float size);
    [[nodiscard]] std::shared_ptr<Texture> loadTexture(const std::string &path);
    [[nodiscard]] std::shared_ptr<Texture> loadNormalMap(const std::string &path);
    [[nodiscard]] std::shared_ptr<Texture> loadRoughnessMap(const std::string &path);

  private:
    constexpr static float DEBUG_FONT_SIZE = 38.f;
    constexpr static float PERF_PANEL_LEFT_MARGIN = 450.f;
    constexpr static int MAX_FRAMES_IN_FLIGHT = 2;
    constexpr static int MAX_TEXTURES = 256;

    Window &window;
    VulkanContext &vkCtx;
    SwapChainHandler swapChainHandler;
    Camera camera;
    InstanceRenderer instanceRenderer;
    TextRenderer textRenderer;
    OcclusionCuller occlusionCuller;

    uint32_t drawCallCount = 0;
    uint64_t vertexCount = 0;

    Debug debug;

    Game *game = nullptr;

    constexpr static uint32_t GPU_QUERY_COUNT = static_cast<uint32_t>(GpuPass::Count) * 2;
    std::vector<vk::raii::QueryPool> gpuQueryPools;
    float timestampPeriodNs = 1.0f;

    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::DescriptorSetLayout normalMapSetLayout = nullptr;
    vk::raii::DescriptorSetLayout roughnessMapSetLayout = nullptr;

    vk::raii::PipelineLayout pipelineLayout = nullptr;

    vk::raii::CommandPool commandPool = nullptr;

    std::vector<vk::raii::CommandBuffer> commandBuffers;
    std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
    std::vector<vk::raii::Fence> inFlightFences;
    uint32_t frameIndex = 0;

    vk::raii::DescriptorPool descriptorPool = nullptr;

    PerFrameBuffer cameraUniformBuffers;

    std::unordered_map<std::shared_ptr<Texture>, std::vector<vk::raii::DescriptorSet>> textureDescriptorSets;
    std::unordered_map<std::shared_ptr<Texture>, std::vector<vk::raii::DescriptorSet>> normalMapDescriptorSets;
    std::unordered_map<std::shared_ptr<Texture>, std::vector<vk::raii::DescriptorSet>> roughnessMapDescriptorSets;

    std::unordered_map<std::string, std::shared_ptr<Texture>> textureCache;
    std::unordered_map<std::string, std::shared_ptr<Texture>> normalMapCache;
    std::unordered_map<std::string, std::shared_ptr<Texture>> roughnessMapCache;
    std::unordered_map<float, std::shared_ptr<Texture>> roughnessFallbackCache;

    std::shared_ptr<Texture> defaultNormalMap;

    bool isInitialized = false;
    bool framebufferResized = false;
    bool isWireframe = false;
    bool isXray = false;
    bool isOcclusionCulled = true;
    bool isPerfOverlayVisible = false;

    std::chrono::high_resolution_clock::time_point engineStartTime;

    void createGraphicsPipeline();
    void createCommandPool();
    void createCommandBuffers();
    void recordCommandBuffer(uint32_t imageIndex);
    void createSyncObjects();
    void drawFrame();

    void createQueryPools();
    void writeTimestamp(GpuPass pass, bool isStart, vk::PipelineStageFlagBits2 stage) const;
    void collectGpuTimings(uint32_t slot);
    DebugFrameInfo makeDebugFrameInfo() const;

    struct UniformBufferObject {
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec4 cameraPos;
    };
    void createDescriptorSetLayout();
    void createCameraUniformBuffer();

    void registerTexture(const std::shared_ptr<Texture> &texture);
    void registerNormalMap(const std::shared_ptr<Texture> &normalMap);
    void registerRoughnessMap(const std::shared_ptr<Texture> &roughnessMap);
    [[nodiscard]] std::shared_ptr<Texture> getOrCreateFlatRoughnessMap(float roughness);

    void updateUniformBuffer(uint32_t currentImage) const;
    void createDescriptorPool();
    void updateInstanceBuffers(uint32_t currentImage);

    struct StringHash {
        using is_transparent = void;
        std::size_t operator()(const std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
    };

    void recreateSwapChain();
    void createOcclusionCuller();

    void update();
};

#endif
