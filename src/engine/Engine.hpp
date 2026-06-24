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
#include "TerrainTypes.hpp"
#include "TextRenderer.hpp"

class TerrainSystem;

class Engine {
  public:
    explicit Engine(Window *_window, VulkanContext *_vkCtx);
    ~Engine();

    void init();
    void run();
    void cleanup();

    void setGame(Game *_game) { game = _game; }

    [[nodiscard]] Camera &getCamera() { return camera; }

    void setLightDirection(const glm::vec3 &direction) { lightDirection = glm::normalize(direction); }
    [[nodiscard]] const glm::vec3 &getLightDirection() const { return lightDirection; }

    RenderObjectHandle addRenderObject(RenderObject object);
    void removeRenderObject(RenderObjectHandle handle);
    [[nodiscard]] RenderObject &getRenderObject(RenderObjectHandle handle);

    struct FBXModel {
        std::vector<std::shared_ptr<Mesh>> meshes;
        std::vector<Material> materials;
    };
    FBXModel createFBXModel(const std::string &fbxPath, const std::string &fileExtension);
    void placeFBXModel(const FBXModel &model, const glm::vec3 &position);

    [[nodiscard]] std::shared_ptr<Mesh> createCubeMesh(float size) const;
    [[nodiscard]] std::shared_ptr<Mesh> createMeshFromData(std::vector<Mesh::Vertex> vertices,
                                                           std::vector<uint32_t> indices) const;
    TerrainSystem &createTerrain(const TerrainConfig &config);
    [[nodiscard]] std::shared_ptr<Texture> loadTexture(const std::string &path) const;
    [[nodiscard]] std::shared_ptr<Texture> loadNormalMap(const std::string &path) const;
    [[nodiscard]] std::shared_ptr<Texture> loadOrmMapFile(const std::string &path) const;
    [[nodiscard]] std::shared_ptr<Texture> loadOrmMap(const std::string &roughnessPath, const std::string &metallicPath,
                                                      const std::string &heightPath) const;

  private:
    constexpr static float DEBUG_FONT_SIZE = 38.f;
    constexpr static float PERF_PANEL_LEFT_MARGIN = 450.f;
    constexpr static int MAX_FRAMES_IN_FLIGHT = 2;
    constexpr static int MAX_TEXTURES = 256;

    constexpr static uint32_t SHADOW_MAP_SIZE = 8192;
    constexpr static float SHADOW_ORTHO_HALF_EXTENT = 100.0f;
    constexpr static float SHADOW_LIGHT_DISTANCE = 150.0f;

    Window &window;
    VulkanContext &vkCtx;
    SwapChainHandler swapChainHandler;
    Camera camera;
    InstanceRenderer instanceRenderer;
    TextRenderer textRenderer;
    OcclusionCuller occlusionCuller;
    std::unique_ptr<TerrainSystem> terrainSystem;

    uint32_t drawCallCount = 0;
    uint64_t vertexCount = 0;

    Debug debug;

    Game *game = nullptr;

    constexpr static uint32_t GPU_QUERY_COUNT = static_cast<uint32_t>(GpuPass::Count) * 2;
    std::vector<vk::raii::QueryPool> gpuQueryPools;
    float timestampPeriodNs = 1.0f;

    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;

    vk::raii::PipelineLayout pipelineLayout = nullptr;

    vk::raii::CommandPool commandPool = nullptr;

    std::vector<vk::raii::CommandBuffer> commandBuffers;
    std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
    std::vector<vk::raii::Fence> inFlightFences;
    uint32_t frameIndex = 0;

    vk::raii::DescriptorPool descriptorPool = nullptr;

    PerFrameBuffer cameraUniformBuffers;

    InstanceRenderer::MapDescriptorSets materialDescriptorSets;

    std::unordered_map<std::string, std::shared_ptr<Texture>> textureCache;
    std::unordered_map<std::string, std::shared_ptr<Texture>> normalMapCache;
    std::unordered_map<std::string, std::shared_ptr<Texture>> ormMapCache;

    struct FloatPairHash {
        std::size_t operator()(const std::pair<float, float> &p) const noexcept {
            return std::hash<float>{}(p.first) ^ (std::hash<float>{}(p.second) << 1);
        }
    };
    std::unordered_map<std::pair<float, float>, std::shared_ptr<Texture>, FloatPairHash> ormFallbackCache;

    std::shared_ptr<Texture> defaultNormalMap;

    glm::vec3 lightDirection = glm::normalize(glm::vec3(0.57735f, 0.57735f, 0.57735f));

    vk::Format shadowDepthFormat = vk::Format::eD32Sfloat;
    vk::raii::Image shadowDepthImage = nullptr;
    vk::raii::DeviceMemory shadowDepthImageMemory = nullptr;
    vk::raii::ImageView shadowDepthImageView = nullptr;
    vk::raii::Sampler shadowSampler = nullptr;

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
        glm::mat4 lightViewProj;
        glm::vec4 lightDir;
    };
    void createDescriptorSetLayout();
    void createCameraUniformBuffer();
    void createShadowResources();
    [[nodiscard]] glm::mat4 computeLightViewProj() const;

    void registerMaterial(const Material &material);
    [[nodiscard]] Material finalizeMaterial(Material material);
    [[nodiscard]] std::shared_ptr<Texture> getOrCreateFlatOrmMap(float roughness, float metallic);
    [[nodiscard]] std::shared_ptr<Texture> loadLinearTexture(const std::string &path) const;

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
