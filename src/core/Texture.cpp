#include "Texture.hpp"

#include "Exceptions.hpp"
#include "VulkanUtils.hpp"

#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

Texture::Texture(const VulkanContext &context) : vkCtx(context) {}

void Texture::createImageView() {
    textureImageView =
        VulkanUtils::createImageView(vkCtx, *textureImage, vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor);
}

void Texture::createSampler() {
    using enum vk::SamplerAddressMode;
    const vk::PhysicalDeviceProperties properties = vkCtx.physicalDevice.getProperties();

    vk::SamplerCreateInfo samplerInfo = {};
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
    samplerInfo.addressModeU = eRepeat;
    samplerInfo.addressModeV = eRepeat;
    samplerInfo.addressModeW = eRepeat;
    samplerInfo.anisotropyEnable = vk::True;
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    samplerInfo.compareEnable = vk::False;
    samplerInfo.compareOp = vk::CompareOp::eAlways;

    textureSampler = vk::raii::Sampler(vkCtx.device, samplerInfo);
}

void Texture::loadFromFile(const std::string &filepath, const vk::raii::CommandPool &commandPool) {
    SDL_Surface *imgSurface = IMG_Load(filepath.c_str());
    if (!imgSurface) {
        throw EngineExceptions::Compatibility("Failed to load texture image '" + filepath + "': " + SDL_GetError());
    }

    if (imgSurface->format != SDL_PIXELFORMAT_RGBA32) {
        SDL_Surface *converted = SDL_ConvertSurface(imgSurface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(imgSurface);
        imgSurface = converted;
        if (!imgSurface) {
            throw EngineExceptions::Compatibility("Failed to convert texture image format.");
        }
    }

    const unsigned int texWidth = imgSurface->w;
    const unsigned int texHeight = imgSurface->h;
    const vk::DeviceSize imageSize = imgSurface->pitch * imgSurface->h;
    void const *pixels = imgSurface->pixels;

    auto [stagingBuffer, stagingBufferMemory] =
        VulkanUtils::createBuffer(vkCtx, imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                                  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void *data = stagingBufferMemory.mapMemory(0, imageSize);
    memcpy(data, pixels, imageSize);
    stagingBufferMemory.unmapMemory();

    SDL_DestroySurface(imgSurface);

    std::tie(textureImage, textureImageMemory) = VulkanUtils::createImage(
        vkCtx, texWidth, texHeight, vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);

    vk::raii::CommandBuffer commandBuffer = VulkanUtils::beginSingleTimeCommands(vkCtx, commandPool);
    VulkanUtils::transitionImageLayout(commandBuffer, textureImage, vk::ImageLayout::eUndefined,
                                       vk::ImageLayout::eTransferDstOptimal);

    VulkanUtils::copyBufferToImage(commandBuffer, stagingBuffer, textureImage, texWidth, texHeight);

    VulkanUtils::transitionImageLayout(commandBuffer, textureImage, vk::ImageLayout::eTransferDstOptimal,
                                       vk::ImageLayout::eShaderReadOnlyOptimal);
    VulkanUtils::endSingleTimeCommands(vkCtx, std::move(commandBuffer));

    createImageView();
    createSampler();
}
