#include "Texture.hpp"

#include "Exceptions.hpp"
#include "VulkanUtils.hpp"

#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

#include <filesystem>
#include <fstream>

Texture::Texture(const VulkanContext &context) : vkCtx(context) {}

namespace {
std::vector<uint8_t> loadChannelFromFile(const std::string &filepath, uint32_t &outWidth, uint32_t &outHeight) {
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

    outWidth = static_cast<uint32_t>(imgSurface->w);
    outHeight = static_cast<uint32_t>(imgSurface->h);

    std::vector<uint8_t> channel(static_cast<size_t>(outWidth) * outHeight);
    const auto *pixels = static_cast<const uint8_t *>(imgSurface->pixels);
    const int bytesPerPixel = SDL_BYTESPERPIXEL(imgSurface->format);

    for (uint32_t y = 0; y < outHeight; ++y) {
        const uint8_t *row = pixels + static_cast<size_t>(y) * imgSurface->pitch;
        for (uint32_t x = 0; x < outWidth; ++x) {
            channel[static_cast<size_t>(y) * outWidth + x] = row[static_cast<size_t>(x) * bytesPerPixel];
        }
    }

    SDL_DestroySurface(imgSurface);
    return channel;
}
} // namespace

namespace {
constexpr uint32_t makeFourCC(const char a, const char b, const char c, const char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

#pragma pack(push, 1)
struct DDSPixelFormat {
    uint32_t size;
    uint32_t flags;
    uint32_t fourCC;
    uint32_t rgbBitCount;
    uint32_t rBitMask;
    uint32_t gBitMask;
    uint32_t bBitMask;
    uint32_t aBitMask;
};

struct DDSHeader {
    uint32_t size;
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitchOrLinearSize;
    uint32_t depth;
    uint32_t mipMapCount;
    std::array<uint32_t, 11> reserved1;
    DDSPixelFormat pixelFormat;
    uint32_t caps;
    uint32_t caps2;
    uint32_t caps3;
    uint32_t caps4;
    uint32_t reserved2;
};

struct DDSHeaderDXT10 {
    uint32_t dxgiFormat;
    uint32_t resourceDimension;
    uint32_t miscFlag;
    uint32_t arraySize;
    uint32_t miscFlags2;
};
#pragma pack(pop)

struct BlockFormatInfo {
    vk::Format format;
    uint32_t blockBytes;
};

BlockFormatInfo blockFormatFromDxgi(const uint32_t dxgiFormat, const bool srgb) {
    using enum vk::Format;

    switch (dxgiFormat) {
    case 71:
    case 72:
        return {srgb ? eBc1RgbaSrgbBlock : eBc1RgbaUnormBlock, 8};
    case 74:
    case 75:
        return {srgb ? eBc2SrgbBlock : eBc2UnormBlock, 16};
    case 77:
    case 78:
        return {srgb ? eBc3SrgbBlock : eBc3UnormBlock, 16};
    case 80:
        return {eBc4UnormBlock, 8};
    case 83:
        return {eBc5UnormBlock, 16};
    case 98:
    case 99:
        return {srgb ? eBc7SrgbBlock : eBc7UnormBlock, 16};
    default:
        throw EngineExceptions::Compatibility(std::format("Unsupported DDS DXGI format ({}).", std::to_string(dxgiFormat)));
    }
}

BlockFormatInfo blockFormatFromFourCC(const uint32_t fourCCValue, const bool srgb) {
    using enum vk::Format;

    if (fourCCValue == makeFourCC('D', 'X', 'T', '1'))
        return {srgb ? eBc1RgbaSrgbBlock : eBc1RgbaUnormBlock, 8};
    if (fourCCValue == makeFourCC('D', 'X', 'T', '3'))
        return {srgb ? eBc2SrgbBlock : eBc2UnormBlock, 16};
    if (fourCCValue == makeFourCC('D', 'X', 'T', '5'))
        return {srgb ? eBc3SrgbBlock : eBc3UnormBlock, 16};
    if (fourCCValue == makeFourCC('A', 'T', 'I', '1') || fourCCValue == makeFourCC('B', 'C', '4', 'U'))
        return {eBc4UnormBlock, 8};
    if (fourCCValue == makeFourCC('A', 'T', 'I', '2') || fourCCValue == makeFourCC('B', 'C', '5', 'U'))
        return {eBc5UnormBlock, 16};
    throw EngineExceptions::Compatibility("Unsupported DDS FourCC.");
}
} // namespace

void Texture::createImageView() {
    textureImageView = VulkanUtils::createImageView(vkCtx, *textureImage, format, vk::ImageAspectFlagBits::eColor, mipLevels);
}

void Texture::createSampler(const bool clampToEdge) {
    using enum vk::SamplerAddressMode;
    const vk::PhysicalDeviceProperties properties = vkCtx.physicalDevice.getProperties();
    const vk::SamplerAddressMode addressMode = clampToEdge ? eClampToEdge : eRepeat;

    vk::SamplerCreateInfo samplerInfo = {};
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    samplerInfo.anisotropyEnable = vk::True;
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    samplerInfo.maxLod = static_cast<float>(mipLevels);
    samplerInfo.compareEnable = vk::False;
    samplerInfo.compareOp = vk::CompareOp::eAlways;

    textureSampler = vk::raii::Sampler(vkCtx.device, samplerInfo);
}

void Texture::loadFromFile(const std::string &filepath, const vk::raii::CommandPool &commandPool, const bool srgb) {
    if (std::filesystem::path(filepath).extension() == ".dds") {
        loadDDS(filepath, commandPool, srgb);
        return;
    }

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

void Texture::loadDDS(const std::string &filepath, const vk::raii::CommandPool &commandPool, const bool srgb) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        throw EngineExceptions::Compatibility("Failed to open DDS texture '" + filepath + "'.");
    }

    const auto fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0);

    std::vector<uint8_t> fileData(fileSize);
    file.read(reinterpret_cast<char *>(fileData.data()), static_cast<std::streamsize>(fileSize));

    constexpr uint32_t ddsMagic = 0x20534444;
    constexpr uint32_t dx10FourCC = makeFourCC('D', 'X', '1', '0');

    if (fileSize < sizeof(uint32_t) + sizeof(DDSHeader)) {
        throw EngineExceptions::Compatibility("'" + filepath + "' is too small to be a valid DDS file.");
    }

    size_t offset = 0;
    uint32_t magic = 0;
    std::memcpy(&magic, fileData.data() + offset, sizeof(magic));
    offset += sizeof(magic);
    if (magic != ddsMagic) {
        throw EngineExceptions::Compatibility("'" + filepath + "' is not a valid DDS file.");
    }

    DDSHeader header{};
    std::memcpy(&header, fileData.data() + offset, sizeof(header));
    offset += sizeof(header);

    BlockFormatInfo blockInfo{};
    if (header.pixelFormat.fourCC == dx10FourCC) {
        if (fileSize < offset + sizeof(DDSHeaderDXT10)) {
            throw EngineExceptions::Compatibility("'" + filepath + "' is missing its DX10 header extension.");
        }
        DDSHeaderDXT10 dxt10{};
        std::memcpy(&dxt10, fileData.data() + offset, sizeof(dxt10));
        offset += sizeof(dxt10);
        blockInfo = blockFormatFromDxgi(dxt10.dxgiFormat, srgb);
    } else {
        blockInfo = blockFormatFromFourCC(header.pixelFormat.fourCC, srgb);
    }

    const uint32_t texWidth = header.width;
    const uint32_t texHeight = header.height;
    const uint32_t mipCount = std::max(header.mipMapCount, 1u);

    std::vector<vk::DeviceSize> mipOffsets(mipCount);
    std::vector<vk::Extent2D> mipExtents(mipCount);

    vk::DeviceSize cursor = 0;
    uint32_t mipWidth = texWidth;
    uint32_t mipHeight = texHeight;
    for (uint32_t i = 0; i < mipCount; ++i) {
        mipOffsets[i] = cursor;
        mipExtents[i] = vk::Extent2D{mipWidth, mipHeight};

        const uint32_t blocksWide = std::max(1u, (mipWidth + 3) / 4);
        const uint32_t blocksHigh = std::max(1u, (mipHeight + 3) / 4);
        cursor += static_cast<vk::DeviceSize>(blocksWide) * blocksHigh * blockInfo.blockBytes;

        mipWidth = std::max(1u, mipWidth / 2);
        mipHeight = std::max(1u, mipHeight / 2);
    }

    const vk::DeviceSize imageDataSize = cursor;
    if (offset + imageDataSize > fileData.size()) {
        throw EngineExceptions::Compatibility("'" + filepath +
                                              "' is truncated: expected more mip data than the file contains.");
    }

    auto [stagingBuffer, stagingBufferMemory] =
        VulkanUtils::createBuffer(vkCtx, imageDataSize, vk::BufferUsageFlagBits::eTransferSrc,
                                  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void *data = stagingBufferMemory.mapMemory(0, imageDataSize);
    memcpy(data, fileData.data() + offset, imageDataSize);
    stagingBufferMemory.unmapMemory();

    width = texWidth;
    height = texHeight;
    mipLevels = mipCount;
    format = blockInfo.format;

    using enum vk::ImageUsageFlagBits;
    VulkanUtils::CreateImageCommand createImageCommand = {};
    createImageCommand.width = texWidth;
    createImageCommand.height = texHeight;
    createImageCommand.mipLevels = mipLevels;
    createImageCommand.samples = vk::SampleCountFlagBits::e1;
    createImageCommand.format = format;
    createImageCommand.tiling = vk::ImageTiling::eOptimal;
    createImageCommand.usage = eTransferDst | eSampled;
    createImageCommand.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

    std::tie(textureImage, textureImageMemory) = VulkanUtils::createImage(vkCtx, createImageCommand);

    const vk::raii::CommandBuffer commandBuffer = VulkanUtils::beginSingleTimeCommands(vkCtx, commandPool);

    VulkanUtils::transitionImageLayout(commandBuffer, textureImage, vk::ImageLayout::eUndefined,
                                       vk::ImageLayout::eTransferDstOptimal, mipLevels);

    std::vector<vk::BufferImageCopy> copyRegions(mipCount);
    for (uint32_t i = 0; i < mipCount; ++i) {
        vk::BufferImageCopy &region = copyRegions[i];
        region.bufferOffset = mipOffsets[i];
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, i, 0, 1);
        region.imageOffset = vk::Offset3D(0, 0, 0);
        region.imageExtent = vk::Extent3D(mipExtents[i].width, mipExtents[i].height, 1);
    }
    commandBuffer.copyBufferToImage(stagingBuffer, textureImage, vk::ImageLayout::eTransferDstOptimal, copyRegions);

    vk::ImageMemoryBarrier readyBarrier = {};
    readyBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    readyBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    readyBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    readyBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    readyBarrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    readyBarrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    readyBarrier.image = textureImage;
    readyBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    readyBarrier.subresourceRange.baseArrayLayer = 0;
    readyBarrier.subresourceRange.layerCount = 1;
    readyBarrier.subresourceRange.baseMipLevel = 0;
    readyBarrier.subresourceRange.levelCount = mipLevels;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {},
                                  readyBarrier);

    VulkanUtils::endSingleTimeCommands(vkCtx, commandBuffer);

    createImageView();
    createSampler();
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

void Texture::createSolidValue3(const glm::vec3 &values, const vk::raii::CommandPool &commandPool) {
    format = vk::Format::eR8G8B8A8Unorm;
    const glm::vec3 byteValues = glm::clamp(values, 0.0f, 1.0f) * 255.0f;
    const std::array<uint8_t, 4> pixel = {static_cast<uint8_t>(byteValues.r), static_cast<uint8_t>(byteValues.g),
                                          static_cast<uint8_t>(byteValues.b), 255};

    constexpr vk::DeviceSize imageSize = 4;
    auto [stagingBuffer, stagingBufferMemory] =
        VulkanUtils::createBuffer(vkCtx, imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                                  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void *data = stagingBufferMemory.mapMemory(0, imageSize);
    memcpy(data, pixel.data(), imageSize);
    stagingBufferMemory.unmapMemory();

    uploadImage(stagingBuffer, 1, 1, commandPool);
}

namespace {
vk::DeviceSize texelSizeForFormat(const vk::Format format) {
    switch (format) {
    case vk::Format::eR8Unorm:
        return 1;
    case vk::Format::eR16Unorm:
    case vk::Format::eR16Sfloat:
        return 2;
    case vk::Format::eR8G8B8A8Unorm:
    case vk::Format::eR8G8B8A8Srgb:
    case vk::Format::eR32Sfloat:
        return 4;
    case vk::Format::eR32G32Sfloat:
        return 8;
    case vk::Format::eR32G32B32A32Sfloat:
        return 16;
    default:
        throw EngineExceptions::Compatibility("Texture::createFromPixels: unsupported pixel format.");
    }
}
} // namespace

void Texture::createFromPixels(const void *pixels, const uint32_t texWidth, const uint32_t texHeight,
                               const vk::Format pixelFormat, const vk::raii::CommandPool &commandPool, const bool clampToEdge) {
    if (const vk::FormatProperties formatProperties = vkCtx.physicalDevice.getFormatProperties(pixelFormat);
        !(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
        throw EngineExceptions::Compatibility("Texture::createFromPixels: format does not support linear sampling on this "
                                              "device.");
    }

    format = pixelFormat;
    width = texWidth;
    height = texHeight;
    mipLevels = 1;

    const vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(texWidth) * texHeight * texelSizeForFormat(pixelFormat);
    auto [stagingBuffer, stagingBufferMemory] =
        VulkanUtils::createBuffer(vkCtx, imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                                  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void *data = stagingBufferMemory.mapMemory(0, imageSize);
    memcpy(data, pixels, imageSize);
    stagingBufferMemory.unmapMemory();

    using enum vk::ImageUsageFlagBits;
    VulkanUtils::CreateImageCommand createImageCommand = {};
    createImageCommand.width = texWidth;
    createImageCommand.height = texHeight;
    createImageCommand.mipLevels = mipLevels;
    createImageCommand.samples = vk::SampleCountFlagBits::e1;
    createImageCommand.format = format;
    createImageCommand.tiling = vk::ImageTiling::eOptimal;
    createImageCommand.usage = eTransferDst | eSampled;
    createImageCommand.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

    std::tie(textureImage, textureImageMemory) = VulkanUtils::createImage(vkCtx, createImageCommand);

    const vk::raii::CommandBuffer commandBuffer = VulkanUtils::beginSingleTimeCommands(vkCtx, commandPool);
    VulkanUtils::transitionImageLayout(commandBuffer, textureImage, vk::ImageLayout::eUndefined,
                                       vk::ImageLayout::eTransferDstOptimal, mipLevels);
    VulkanUtils::copyBufferToImage(commandBuffer, stagingBuffer, textureImage, texWidth, texHeight);
    VulkanUtils::transitionImageLayout(commandBuffer, textureImage, vk::ImageLayout::eTransferDstOptimal,
                                       vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels);
    VulkanUtils::endSingleTimeCommands(vkCtx, commandBuffer);

    createImageView();
    createSampler(clampToEdge);
}

Texture::PendingUpload Texture::createFromPixelsRecorded(const void *pixels, const uint32_t texWidth, const uint32_t texHeight,
                                                         const vk::Format pixelFormat,
                                                         const vk::raii::CommandBuffer &commandBuffer, const bool clampToEdge) {
    if (const vk::FormatProperties formatProperties = vkCtx.physicalDevice.getFormatProperties(pixelFormat);
        !(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
        throw EngineExceptions::Compatibility("Texture::createFromPixelsRecorded: format does not support linear sampling "
                                              "on this device.");
    }

    format = pixelFormat;
    width = texWidth;
    height = texHeight;
    mipLevels = 1;

    const vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(texWidth) * texHeight * texelSizeForFormat(pixelFormat);
    PendingUpload upload;
    std::tie(upload.stagingBuffer, upload.stagingBufferMemory) =
        VulkanUtils::createBuffer(vkCtx, imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                                  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void *data = upload.stagingBufferMemory.mapMemory(0, imageSize);
    memcpy(data, pixels, imageSize);
    upload.stagingBufferMemory.unmapMemory();

    using enum vk::ImageUsageFlagBits;
    VulkanUtils::CreateImageCommand createImageCommand = {};
    createImageCommand.width = texWidth;
    createImageCommand.height = texHeight;
    createImageCommand.mipLevels = mipLevels;
    createImageCommand.samples = vk::SampleCountFlagBits::e1;
    createImageCommand.format = format;
    createImageCommand.tiling = vk::ImageTiling::eOptimal;
    createImageCommand.usage = eTransferDst | eSampled;
    createImageCommand.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

    std::tie(textureImage, textureImageMemory) = VulkanUtils::createImage(vkCtx, createImageCommand);

    VulkanUtils::transitionImageLayout(commandBuffer, textureImage, vk::ImageLayout::eUndefined,
                                       vk::ImageLayout::eTransferDstOptimal, mipLevels);
    VulkanUtils::copyBufferToImage(commandBuffer, upload.stagingBuffer, textureImage, texWidth, texHeight);
    VulkanUtils::transitionImageLayout(commandBuffer, textureImage, vk::ImageLayout::eTransferDstOptimal,
                                       vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels);

    createImageView();
    createSampler(clampToEdge);

    return upload;
}

void Texture::createStorageTarget(const uint32_t texWidth, const uint32_t texHeight, const vk::Format targetFormat,
                                  const vk::raii::CommandBuffer &commandBuffer, const bool clampToEdge) {
    if (const vk::FormatProperties formatProperties = vkCtx.physicalDevice.getFormatProperties(targetFormat);
        !(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eStorageImage) ||
        !(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
        throw EngineExceptions::Compatibility("Texture::createStorageTarget: format does not support storage + linear "
                                              "sampling on this device.");
    }

    format = targetFormat;
    width = texWidth;
    height = texHeight;
    mipLevels = 1;

    using enum vk::ImageUsageFlagBits;
    VulkanUtils::CreateImageCommand createImageCommand = {};
    createImageCommand.width = texWidth;
    createImageCommand.height = texHeight;
    createImageCommand.mipLevels = mipLevels;
    createImageCommand.samples = vk::SampleCountFlagBits::e1;
    createImageCommand.format = format;
    createImageCommand.tiling = vk::ImageTiling::eOptimal;
    createImageCommand.usage = eStorage | eSampled;
    createImageCommand.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

    std::tie(textureImage, textureImageMemory) = VulkanUtils::createImage(vkCtx, createImageCommand);

    VulkanUtils::ImageBarrierCommand toGeneral{};
    toGeneral.image = *textureImage;
    toGeneral.old_layout = vk::ImageLayout::eUndefined;
    toGeneral.new_layout = vk::ImageLayout::eGeneral;
    toGeneral.src_access_mask = {};
    toGeneral.dst_access_mask = vk::AccessFlagBits2::eShaderWrite;
    toGeneral.src_stage_mask = vk::PipelineStageFlagBits2::eTopOfPipe;
    toGeneral.dst_stage_mask = vk::PipelineStageFlagBits2::eComputeShader;
    toGeneral.image_aspect_flags = vk::ImageAspectFlagBits::eColor;
    toGeneral.base_mip_level = 0;
    toGeneral.level_count = 1;

    VulkanUtils::imageBarriers(commandBuffer, {toGeneral});

    createImageView();
    createSampler(clampToEdge);
}

void Texture::loadPackedChannels(const std::string &rPath, const float rFallback, const std::string &gPath,
                                 const float gFallback, const std::string &bPath, const float bFallback,
                                 const vk::raii::CommandPool &commandPool) {
    uint32_t texWidth = 0;
    uint32_t texHeight = 0;

    std::vector<uint8_t> rChannel;
    std::vector<uint8_t> gChannel;
    std::vector<uint8_t> bChannel;

    const auto loadChannel = [&](const std::string &path, std::vector<uint8_t> &out) {
        if (path.empty())
            return;

        uint32_t w = 0;
        uint32_t h = 0;
        out = loadChannelFromFile(path, w, h);

        if (texWidth == 0) {
            texWidth = w;
            texHeight = h;
        } else if (w != texWidth || h != texHeight) {
            throw EngineExceptions::Compatibility("Packed texture channels must share the same resolution (mismatch in '" +
                                                  path + "').");
        }
    };

    loadChannel(rPath, rChannel);
    loadChannel(gPath, gChannel);
    loadChannel(bPath, bChannel);

    if (texWidth == 0) {
        createSolidValue3(glm::vec3(rFallback, gFallback, bFallback), commandPool);
        return;
    }

    const auto fillFallback = [&](std::vector<uint8_t> &out, const float value) {
        if (!out.empty())
            return;
        out.assign(static_cast<size_t>(texWidth) * texHeight, static_cast<uint8_t>(glm::clamp(value, 0.0f, 1.0f) * 255.0f));
    };

    fillFallback(rChannel, rFallback);
    fillFallback(gChannel, gFallback);
    fillFallback(bChannel, bFallback);

    const vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(texWidth) * texHeight * 4;
    auto [stagingBuffer, stagingBufferMemory] =
        VulkanUtils::createBuffer(vkCtx, imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                                  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    auto *dst = static_cast<uint8_t *>(stagingBufferMemory.mapMemory(0, imageSize));
    const size_t pixelCount = static_cast<size_t>(texWidth) * texHeight;
    for (size_t i = 0; i < pixelCount; ++i) {
        dst[i * 4 + 0] = rChannel[i];
        dst[i * 4 + 1] = gChannel[i];
        dst[i * 4 + 2] = bChannel[i];
        dst[i * 4 + 3] = 255;
    }
    stagingBufferMemory.unmapMemory();

    format = vk::Format::eR8G8B8A8Unorm;
    uploadImage(stagingBuffer, texWidth, texHeight, commandPool);
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
