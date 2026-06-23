#include "Texture.hpp"

#include "Exceptions.hpp"
#include "VulkanUtils.hpp"

#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

Texture::Texture(const VulkanContext &context) : vkCtx(context) {}

void Texture::createImageView() {
    textureImageView = VulkanUtils::createImageView(vkCtx, *textureImage, format, vk::ImageAspectFlagBits::eColor, mipLevels);
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

void Texture::loadFromFile(const std::string &filepath, const vk::raii::CommandPool &commandPool, const bool srgb) {
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

    const auto texWidth = static_cast<uint32_t>(imgSurface->w);
    const auto texHeight = static_cast<uint32_t>(imgSurface->h);
    const vk::DeviceSize imageSize = imgSurface->pitch * imgSurface->h;

    auto [stagingBuffer, stagingBufferMemory] =
        VulkanUtils::createBuffer(vkCtx, imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                                  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void *data = stagingBufferMemory.mapMemory(0, imageSize);
    memcpy(data, imgSurface->pixels, imageSize);
    stagingBufferMemory.unmapMemory();

    SDL_DestroySurface(imgSurface);

    format = srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
    uploadImage(stagingBuffer, texWidth, texHeight, commandPool);
}

void Texture::createSolidColor(const glm::vec3 &color, const vk::raii::CommandPool &commandPool) {
    format = vk::Format::eR8G8B8A8Srgb;
    const glm::vec3 encoded = glm::pow(glm::clamp(color, 0.0f, 1.0f), glm::vec3(1.0f / 2.2f));

    const std::array<uint8_t, 4> pixel = {static_cast<uint8_t>(encoded.r * 255.0f), static_cast<uint8_t>(encoded.g * 255.0f),
                                          static_cast<uint8_t>(encoded.b * 255.0f), 255};

    constexpr vk::DeviceSize imageSize = 4;
    auto [stagingBuffer, stagingBufferMemory] =
        VulkanUtils::createBuffer(vkCtx, imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                                  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void *data = stagingBufferMemory.mapMemory(0, imageSize);
    memcpy(data, pixel.data(), imageSize);
    stagingBufferMemory.unmapMemory();

    uploadImage(stagingBuffer, 1, 1, commandPool);
}

void Texture::createFlatNormalMap(const vk::raii::CommandPool &commandPool) {
    format = vk::Format::eR8G8B8A8Unorm;
    constexpr std::array<uint8_t, 4> pixel = {128, 128, 255, 255};

    constexpr vk::DeviceSize imageSize = 4;
    auto [stagingBuffer, stagingBufferMemory] =
        VulkanUtils::createBuffer(vkCtx, imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                                  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void *data = stagingBufferMemory.mapMemory(0, imageSize);
    memcpy(data, pixel.data(), imageSize);
    stagingBufferMemory.unmapMemory();

    uploadImage(stagingBuffer, 1, 1, commandPool);
}

void Texture::createSolidValue(const float value, const vk::raii::CommandPool &commandPool) {
    format = vk::Format::eR8G8B8A8Unorm;
    const auto byteValue = static_cast<uint8_t>(glm::clamp(value, 0.0f, 1.0f) * 255.0f);
    const std::array<uint8_t, 4> pixel = {byteValue, byteValue, byteValue, 255};

    constexpr vk::DeviceSize imageSize = 4;
    auto [stagingBuffer, stagingBufferMemory] =
        VulkanUtils::createBuffer(vkCtx, imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                                  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void *data = stagingBufferMemory.mapMemory(0, imageSize);
    memcpy(data, pixel.data(), imageSize);
    stagingBufferMemory.unmapMemory();

    uploadImage(stagingBuffer, 1, 1, commandPool);
}

void Texture::uploadImage(const vk::raii::Buffer &stagingBuffer, const uint32_t texWidth, const uint32_t texHeight,
                          const vk::raii::CommandPool &commandPool) {
    width = texWidth;
    height = texHeight;
    mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

    using enum vk::ImageUsageFlagBits;
    VulkanUtils::CreateImageCommand createImageCommand = {};
    createImageCommand.width = texWidth;
    createImageCommand.height = texHeight;
    createImageCommand.mipLevels = mipLevels;
    createImageCommand.samples = vk::SampleCountFlagBits::e1;
    createImageCommand.format = vk::Format::eR8G8B8A8Srgb;
    createImageCommand.tiling = vk::ImageTiling::eOptimal;
    createImageCommand.usage = eTransferSrc | eTransferDst | eSampled;
    createImageCommand.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
    createImageCommand.format = format;

    std::tie(textureImage, textureImageMemory) = VulkanUtils::createImage(vkCtx, createImageCommand);

    const vk::raii::CommandBuffer commandBuffer = VulkanUtils::beginSingleTimeCommands(vkCtx, commandPool);
    VulkanUtils::transitionImageLayout(commandBuffer, textureImage, vk::ImageLayout::eUndefined,
                                       vk::ImageLayout::eTransferDstOptimal, mipLevels);
    VulkanUtils::copyBufferToImage(commandBuffer, stagingBuffer, textureImage, texWidth, texHeight);
    generateMipmaps(vk::Format::eR8G8B8A8Srgb, commandBuffer);
    VulkanUtils::endSingleTimeCommands(vkCtx, commandBuffer);

    createImageView();
    createSampler();
}

void Texture::generateMipmaps(const vk::Format imageFormat, const vk::raii::CommandBuffer &commandBuffer) const {
    if (const vk::FormatProperties formatProperties = vkCtx.physicalDevice.getFormatProperties(imageFormat);
        !(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
        throw EngineExceptions::Compatibility("Texture image format does not support linear blitting.");
    }

    vk::ImageMemoryBarrier barrier = {};
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.image = textureImage;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    auto mipWidth = static_cast<int32_t>(width);
    auto mipHeight = static_cast<int32_t>(height);

    for (uint32_t i = 1; i < mipLevels; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
                                      barrier);

        vk::ArrayWrapper1D<vk::Offset3D, 2> offsets;
        vk::ArrayWrapper1D<vk::Offset3D, 2> dstOffsets;
        offsets[0] = vk::Offset3D(0, 0, 0);
        offsets[1] = vk::Offset3D(mipWidth, mipHeight, 1);
        dstOffsets[0] = vk::Offset3D(0, 0, 0);
        dstOffsets[1] = vk::Offset3D(mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1);
        vk::ImageBlit blit = {};
        blit.srcOffsets = offsets;
        blit.dstOffsets = dstOffsets;
        blit.srcSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i - 1, 0, 1);
        blit.dstSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i, 0, 1);

        commandBuffer.blitImage(textureImage, vk::ImageLayout::eTransferSrcOptimal, textureImage,
                                vk::ImageLayout::eTransferDstOptimal, {blit}, vk::Filter::eLinear);

        barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {},
                                      {}, barrier);

        if (mipWidth > 1)
            mipWidth /= 2;
        if (mipHeight > 1)
            mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {},
                                  barrier);
}
