#ifndef TEXTURE_HPP
#define TEXTURE_HPP

class Texture {
  public:
    explicit Texture(const VulkanContext &context);
    ~Texture() = default;

    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;

    void loadFromFile(const std::string &filepath, const vk::raii::CommandPool &commandPool);

    vk::raii::ImageView textureImageView = nullptr;
    vk::raii::Sampler textureSampler = nullptr;

    uint32_t mipLevels = 0;

  private:
    const VulkanContext &vkCtx;

    uint32_t width = 0;
    uint32_t height = 0;

    vk::raii::Image textureImage = nullptr;
    vk::raii::DeviceMemory textureImageMemory = nullptr;

    void createImageView();
    void createSampler();
    void generateMipmaps(vk::Format imageFormat, const vk::raii::CommandBuffer &commandBuffer) const;
};

#endif