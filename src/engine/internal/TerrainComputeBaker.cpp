#include "TerrainComputeBaker.hpp"
#include "DescriptorWriter.hpp"
#include "PipelineBuilder.hpp"
#include "VulkanUtils.hpp"
#include <array>

TerrainComputeBaker::TerrainComputeBaker(VulkanContext &vkCtx) : vkCtx(vkCtx) {}

void TerrainComputeBaker::createResources(const int requestedMaxPaddedResolution, const int expectedConcurrentBakes) {
    maxPaddedResolution = std::max(requestedMaxPaddedResolution, 4);

    createDescriptorSetLayouts();

    const auto setsU = static_cast<uint32_t>(std::max(expectedConcurrentBakes, 1));
    const std::array<vk::DescriptorPoolSize, 1> poolSizes = {
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, setsU * 4},
    };

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = setsU * 2;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    descriptorPool = vk::raii::DescriptorPool(vkCtx.device, poolInfo);
}

void TerrainComputeBaker::createDescriptorSetLayouts() {
    vk::DescriptorSetLayoutBinding heightBinding{};
    heightBinding.binding = 0;
    heightBinding.descriptorType = vk::DescriptorType::eStorageImage;
    heightBinding.descriptorCount = 1;
    heightBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    heightPassSetLayout = VulkanUtils::createDescriptorSetLayout(vkCtx, {heightBinding});

    vk::DescriptorSetLayoutBinding paddedBinding{};
    paddedBinding.binding = 0;
    paddedBinding.descriptorType = vk::DescriptorType::eStorageImage;
    paddedBinding.descriptorCount = 1;
    paddedBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutBinding outHeightBinding{};
    outHeightBinding.binding = 1;
    outHeightBinding.descriptorType = vk::DescriptorType::eStorageImage;
    outHeightBinding.descriptorCount = 1;
    outHeightBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutBinding outNormalBinding{};
    outNormalBinding.binding = 2;
    outNormalBinding.descriptorType = vk::DescriptorType::eStorageImage;
    outNormalBinding.descriptorCount = 1;
    outNormalBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    normalPassSetLayout = VulkanUtils::createDescriptorSetLayout(vkCtx, {paddedBinding, outHeightBinding, outNormalBinding});
}

void TerrainComputeBaker::createPipelines() {
    vk::PushConstantRange heightPushRange{};
    heightPushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    heightPushRange.size = sizeof(TerrainBakePushConstants);
    heightPassPipelineLayout = VulkanUtils::createPipelineLayout(vkCtx, {*heightPassSetLayout}, {heightPushRange});

    vk::PushConstantRange normalPushRange{};
    normalPushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    normalPushRange.size = sizeof(TerrainBakeResolvePushConstants);
    normalPassPipelineLayout = VulkanUtils::createPipelineLayout(vkCtx, {*normalPassSetLayout}, {normalPushRange});

    const ComputePipelineBuilder builder(vkCtx);
    heightPassPipeline = builder.build("shaders/terrain/TerrainBakeHeight.spv", *heightPassPipelineLayout);
    normalPassPipeline = builder.build("shaders/terrain/TerrainBakeNormal.spv", *normalPassPipelineLayout);
}

TerrainComputeBaker::BakeResources TerrainComputeBaker::allocateBakeResources() const {
    BakeResources resources;

    using enum vk::ImageUsageFlagBits;
    VulkanUtils::CreateImageCommand imageCommand = {};
    imageCommand.width = static_cast<uint32_t>(maxPaddedResolution);
    imageCommand.height = static_cast<uint32_t>(maxPaddedResolution);
    imageCommand.mipLevels = 1;
    imageCommand.samples = vk::SampleCountFlagBits::e1;
    imageCommand.format = vk::Format::eR32Sfloat;
    imageCommand.tiling = vk::ImageTiling::eOptimal;
    imageCommand.usage = eStorage;
    imageCommand.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

    std::tie(resources.paddedHeightImage, resources.paddedHeightMemory) = VulkanUtils::createImage(vkCtx, imageCommand);
    resources.paddedHeightView = VulkanUtils::createImageView(vkCtx, resources.paddedHeightImage, vk::Format::eR32Sfloat,
                                                              vk::ImageAspectFlagBits::eColor, 1);

    vk::DescriptorSetAllocateInfo heightAllocInfo{};
    heightAllocInfo.descriptorPool = *descriptorPool;
    heightAllocInfo.descriptorSetCount = 1;
    heightAllocInfo.pSetLayouts = &*heightPassSetLayout;
    resources.heightPassSet = std::move(vkCtx.device.allocateDescriptorSets(heightAllocInfo).front());

    vk::DescriptorSetAllocateInfo normalAllocInfo{};
    normalAllocInfo.descriptorPool = *descriptorPool;
    normalAllocInfo.descriptorSetCount = 1;
    normalAllocInfo.pSetLayouts = &*normalPassSetLayout;
    resources.normalPassSet = std::move(vkCtx.device.allocateDescriptorSets(normalAllocInfo).front());

    return resources;
}

TerrainComputeBaker::BakeResources TerrainComputeBaker::dispatchBake(const BakeCommand &command) const {
    using enum vk::PipelineStageFlagBits2;
    constexpr uint32_t GROUP = 8;

    BakeResources resources = allocateBakeResources();
    const int paddedResolution = command.resolution + 2;

    DescriptorWriter setupWriter(vkCtx);
    setupWriter.writeImage(*resources.heightPassSet, 0, vk::DescriptorType::eStorageImage, *resources.paddedHeightView, nullptr,
                           vk::ImageLayout::eGeneral);
    setupWriter.writeImage(*resources.normalPassSet, 0, vk::DescriptorType::eStorageImage, *resources.paddedHeightView, nullptr,
                           vk::ImageLayout::eGeneral);
    setupWriter.writeImage(*resources.normalPassSet, 1, vk::DescriptorType::eStorageImage, command.outHeightView, nullptr,
                           vk::ImageLayout::eGeneral);
    setupWriter.writeImage(*resources.normalPassSet, 2, vk::DescriptorType::eStorageImage, command.outNormalView, nullptr,
                           vk::ImageLayout::eGeneral);
    setupWriter.update();

    VulkanUtils::ImageBarrierCommand scratchToWrite = {};
    scratchToWrite.image = *resources.paddedHeightImage;
    scratchToWrite.old_layout = vk::ImageLayout::eUndefined;
    scratchToWrite.new_layout = vk::ImageLayout::eGeneral;
    scratchToWrite.src_access_mask = {};
    scratchToWrite.dst_access_mask = vk::AccessFlagBits2::eShaderWrite;
    scratchToWrite.src_stage_mask = eTopOfPipe;
    scratchToWrite.dst_stage_mask = eComputeShader;
    scratchToWrite.image_aspect_flags = vk::ImageAspectFlagBits::eColor;
    scratchToWrite.base_mip_level = 0;
    scratchToWrite.level_count = 1;

    VulkanUtils::ImageBarrierCommand outToWrite = {};
    outToWrite.old_layout = vk::ImageLayout::eUndefined;
    outToWrite.new_layout = vk::ImageLayout::eGeneral;
    outToWrite.src_access_mask = {};
    outToWrite.dst_access_mask = vk::AccessFlagBits2::eShaderWrite;
    outToWrite.src_stage_mask = eTopOfPipe;
    outToWrite.dst_stage_mask = eComputeShader;
    outToWrite.image_aspect_flags = vk::ImageAspectFlagBits::eColor;
    outToWrite.base_mip_level = 0;
    outToWrite.level_count = 1;

    VulkanUtils::ImageBarrierCommand heightOutToWrite = outToWrite;
    heightOutToWrite.image = command.outHeightImage;
    VulkanUtils::ImageBarrierCommand normalOutToWrite = outToWrite;
    normalOutToWrite.image = command.outNormalImage;

    VulkanUtils::imageBarriers(*command.commandBuffer, {scratchToWrite, heightOutToWrite, normalOutToWrite});

    TerrainBakePushConstants heightPc{};
    heightPc.chunkMin = command.chunkMin;
    heightPc.texelWorldSize = command.texelWorldSize;
    heightPc.paddedResolution = paddedResolution;
    heightPc.noiseScale = command.noise.scale;
    heightPc.heightScale = command.noise.heightScale;
    heightPc.baseHeight = command.noise.baseHeight;
    heightPc.persistence = command.noise.persistence;
    heightPc.lacunarity = command.noise.lacunarity;
    heightPc.octaves = command.noise.octaves;
    heightPc.ridgeSharpness = command.noise.ridgeSharpness;
    heightPc.heightRedistribution = command.noise.heightRedistribution;
    heightPc.regionScale = command.noise.regionScale;
    heightPc.regionThreshold = command.noise.regionThreshold;
    heightPc.regionBlendWidth = command.noise.regionBlendWidth;
    heightPc.flatScale = command.noise.flatScale;
    heightPc.flatThreshold = command.noise.flatThreshold;
    heightPc.flatBlendWidth = command.noise.flatBlendWidth;
    heightPc.minRelief = command.noise.minRelief;
    heightPc.noiseOffset = command.noise.offset;

    command.commandBuffer->bindPipeline(vk::PipelineBindPoint::eCompute, *heightPassPipeline);
    command.commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eCompute, *heightPassPipelineLayout, 0,
                                              *resources.heightPassSet, nullptr);
    command.commandBuffer->pushConstants<TerrainBakePushConstants>(*heightPassPipelineLayout, vk::ShaderStageFlagBits::eCompute,
                                                                   0, heightPc);
    command.commandBuffer->dispatch(static_cast<uint32_t>(paddedResolution + GROUP - 1) / GROUP,
                                    static_cast<uint32_t>(paddedResolution + GROUP - 1) / GROUP, 1);

    VulkanUtils::ImageBarrierCommand scratchToRead = {};
    scratchToRead.image = *resources.paddedHeightImage;
    scratchToRead.old_layout = vk::ImageLayout::eGeneral;
    scratchToRead.new_layout = vk::ImageLayout::eGeneral;
    scratchToRead.src_access_mask = vk::AccessFlagBits2::eShaderWrite;
    scratchToRead.dst_access_mask = vk::AccessFlagBits2::eShaderRead;
    scratchToRead.src_stage_mask = eComputeShader;
    scratchToRead.dst_stage_mask = eComputeShader;
    scratchToRead.image_aspect_flags = vk::ImageAspectFlagBits::eColor;
    scratchToRead.base_mip_level = 0;
    scratchToRead.level_count = 1;

    VulkanUtils::imageBarriers(*command.commandBuffer, {scratchToRead});

    TerrainBakeResolvePushConstants normalPc{};
    normalPc.resolution = command.resolution;
    normalPc.texelWorldSize = command.texelWorldSize;

    command.commandBuffer->bindPipeline(vk::PipelineBindPoint::eCompute, *normalPassPipeline);
    command.commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eCompute, *normalPassPipelineLayout, 0,
                                              *resources.normalPassSet, nullptr);
    command.commandBuffer->pushConstants<TerrainBakeResolvePushConstants>(*normalPassPipelineLayout,
                                                                          vk::ShaderStageFlagBits::eCompute, 0, normalPc);
    command.commandBuffer->dispatch(static_cast<uint32_t>(command.resolution + GROUP - 1) / GROUP,
                                    static_cast<uint32_t>(command.resolution + GROUP - 1) / GROUP, 1);

    VulkanUtils::ImageBarrierCommand heightToRead = {};
    heightToRead.image = command.outHeightImage;
    heightToRead.old_layout = vk::ImageLayout::eGeneral;
    heightToRead.new_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    heightToRead.src_access_mask = vk::AccessFlagBits2::eShaderWrite;
    heightToRead.dst_access_mask = vk::AccessFlagBits2::eShaderRead;
    heightToRead.src_stage_mask = eComputeShader;
    heightToRead.dst_stage_mask = eVertexShader;
    heightToRead.image_aspect_flags = vk::ImageAspectFlagBits::eColor;
    heightToRead.base_mip_level = 0;
    heightToRead.level_count = 1;

    VulkanUtils::ImageBarrierCommand normalToRead = heightToRead;
    normalToRead.image = command.outNormalImage;

    VulkanUtils::imageBarriers(*command.commandBuffer, {heightToRead, normalToRead});

    return resources;
}
