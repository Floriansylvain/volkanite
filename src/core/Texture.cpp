#include "Texture.hpp"

#include "Exceptions.hpp"
#include "VulkanUtils.hpp"

#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

Texture::Texture(const VulkanContext &context) : vkCtx(context) {}

void Texture::createImageView() {
    textureImageView = VulkanUtils::createImageView(vkCtx, *textureImage, vk::Format::eR8G8B8A8Srgb,
                                                    vk::ImageAspectFlagBits::eColor, mipLevels);
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

    width = texWidth;
    height = texHeight;
    mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

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
        vkCtx,
        {texWidth, texHeight, mipLevels, vk::SampleCountFlagBits::e1, vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal,
         vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
         vk::MemoryPropertyFlagBits::eDeviceLocal});

    vk::raii::CommandBuffer commandBuffer = VulkanUtils::beginSingleTimeCommands(vkCtx, commandPool);
    VulkanUtils::transitionImageLayout(commandBuffer, textureImage, vk::ImageLayout::eUndefined,
                                       vk::ImageLayout::eTransferDstOptimal, mipLevels);
    VulkanUtils::copyBufferToImage(commandBuffer, stagingBuffer, textureImage, texWidth, texHeight);
    generateMipmaps(vk::Format::eR8G8B8A8Srgb, commandBuffer);
    VulkanUtils::endSingleTimeCommands(vkCtx, std::move(commandBuffer));

    createImageView();
    createSampler();
}

void Texture::generateMipmaps(vk::Format imageFormat, const vk::raii::CommandBuffer &commandBuffer) {
    vk::FormatProperties formatProperties = vkCtx.physicalDevice.getFormatProperties(imageFormat);

    if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
        throw std::runtime_error("Texture image format does not support linear blitting.");
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

    int32_t mipWidth = width;
    int32_t mipHeight = height;

    for (uint32_t i = 1; i < mipLevels; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
                                      barrier);

        vk::ArrayWrapper1D<vk::Offset3D, 2> offsets, dstOffsets;
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
