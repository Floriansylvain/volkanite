#ifndef TEXTURE_HPP
#define TEXTURE_HPP

class Texture {
  public:
    explicit Texture(const VulkanContext &context);
    ~Texture() = default;

    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;

    void loadFromFile(const std::string &filepath, const vk::raii::CommandPool &commandPool, bool srgb = true);
    void createSolidColor(const glm::vec3 &color, const vk::raii::CommandPool &commandPool);
    void createFlatNormalMap(const vk::raii::CommandPool &commandPool);
    void createSolidValue(float value, const vk::raii::CommandPool &commandPool);
    void createSolidValue3(const glm::vec3 &values, const vk::raii::CommandPool &commandPool);

    void createFromPixels(const void *pixels, uint32_t texWidth, uint32_t texHeight, vk::Format pixelFormat,
                          const vk::raii::CommandPool &commandPool, bool clampToEdge = true);

    struct PendingUpload {
        vk::raii::Buffer stagingBuffer = nullptr;
        vk::raii::DeviceMemory stagingBufferMemory = nullptr;
    };
    [[nodiscard]] PendingUpload createFromPixelsRecorded(const void *pixels, uint32_t texWidth, uint32_t texHeight,
                                                         vk::Format pixelFormat, const vk::raii::CommandBuffer &commandBuffer,
                                                         bool clampToEdge = true);

    void createStorageTarget(uint32_t texWidth, uint32_t texHeight, vk::Format targetFormat,
                             const vk::raii::CommandBuffer &commandBuffer, bool clampToEdge = true);

    [[nodiscard]] vk::Image getImage() const { return *textureImage; }

    void loadPackedChannels(const std::string &rPath, float rFallback, const std::string &gPath, float gFallback,
                            const std::string &bPath, float bFallback, const vk::raii::CommandPool &commandPool);

    vk::raii::ImageView textureImageView = nullptr;
    vk::raii::Sampler textureSampler = nullptr;

    uint32_t mipLevels = 0;

  private:
    const VulkanContext &vkCtx;

    uint32_t width = 0;
    uint32_t height = 0;

    vk::raii::Image textureImage = nullptr;
    vk::raii::DeviceMemory textureImageMemory = nullptr;

    vk::Format format = vk::Format::eR8G8B8A8Srgb;

    void createImageView();
    void createSampler(bool clampToEdge = false);
    void generateMipmaps(vk::Format imageFormat, const vk::raii::CommandBuffer &commandBuffer) const;
    void uploadImage(const vk::raii::Buffer &stagingBuffer, uint32_t texWidth, uint32_t texHeight,
                     const vk::raii::CommandPool &commandPool);

    void loadDDS(const std::string &filepath, const vk::raii::CommandPool &commandPool, bool srgb);
};

#endif
