#include "OcclusionCuller.hpp"
#include "DescriptorWriter.hpp"
#include "PipelineBuilder.hpp"
#include "VulkanUtils.hpp"
#include <array>
#include <cmath>

void OcclusionCuller::init() {
    using enum vk::SamplerAddressMode;

    createDescriptorSetLayouts();

    vk::SamplerCreateInfo depthSamplerInfo{};
    depthSamplerInfo.magFilter = vk::Filter::eNearest;
    depthSamplerInfo.minFilter = vk::Filter::eNearest;
    depthSamplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
    depthSamplerInfo.addressModeU = eClampToEdge;
    depthSamplerInfo.addressModeV = eClampToEdge;
    depthSamplerInfo.addressModeW = eClampToEdge;
    depthSampler = vk::raii::Sampler(vkCtx.device, depthSamplerInfo);

    vk::SamplerCreateInfo hiZSamplerInfo{};
    hiZSamplerInfo.magFilter = vk::Filter::eNearest;
    hiZSamplerInfo.minFilter = vk::Filter::eNearest;
    hiZSamplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
    hiZSamplerInfo.addressModeU = eClampToEdge;
    hiZSamplerInfo.addressModeV = eClampToEdge;
    hiZSamplerInfo.minLod = 0.0f;
    hiZSamplerInfo.maxLod = 16.0f;
    hiZSampler = vk::raii::Sampler(vkCtx.device, hiZSamplerInfo);
}

OcclusionCuller::OcclusionCuller(VulkanContext &context, const int maxFramesInFlight)
    : vkCtx(context), maxFramesInFlight(maxFramesInFlight) {}

void OcclusionCuller::createResources(const vk::Extent2D _extent, const vk::Format depthFormat) {
    extent = _extent;

    mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(extent.width, extent.height)))) + 1;
    mipExtents.clear();
    mipExtents.reserve(mipLevels);
    for (uint32_t i = 0; i < mipLevels; ++i) {
        mipExtents.push_back({std::max(1u, extent.width >> i), std::max(1u, extent.height >> i)});
    }

    resolvedDepthImages.clear();
    resolvedDepthImageMemories.clear();
    resolvedDepthImageViews.clear();
    hiZImages.clear();
    hiZImageMemories.clear();
    hiZMipViews.clear();
    hiZFullViews.clear();
    hiZMipViews.resize(maxFramesInFlight);

    for (int f = 0; f < maxFramesInFlight; ++f) {
        using enum vk::ImageUsageFlagBits;

        VulkanUtils::CreateImageCommand depthImageCommand = {};
        depthImageCommand.width = extent.width;
        depthImageCommand.height = extent.height;
        depthImageCommand.mipLevels = 1;
        depthImageCommand.samples = vk::SampleCountFlagBits::e1;
        depthImageCommand.format = depthFormat;
        depthImageCommand.tiling = vk::ImageTiling::eOptimal;
        depthImageCommand.usage = eDepthStencilAttachment | eSampled;
        depthImageCommand.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        auto [depthImage, depthMemory] = VulkanUtils::createImage(vkCtx, depthImageCommand);
        vk::raii::ImageView depthView =
            VulkanUtils::createImageView(vkCtx, depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);

        resolvedDepthImages.push_back(std::move(depthImage));
        resolvedDepthImageMemories.push_back(std::move(depthMemory));
        resolvedDepthImageViews.push_back(std::move(depthView));

        VulkanUtils::CreateImageCommand pyramidImageCommand = {};
        pyramidImageCommand.width = extent.width;
        pyramidImageCommand.height = extent.height;
        pyramidImageCommand.mipLevels = mipLevels;
        pyramidImageCommand.samples = vk::SampleCountFlagBits::e1;
        pyramidImageCommand.format = vk::Format::eR32Sfloat;
        pyramidImageCommand.tiling = vk::ImageTiling::eOptimal;
        pyramidImageCommand.usage = eStorage | eSampled;
        pyramidImageCommand.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        auto [pyramidImage, pyramidMemory] = VulkanUtils::createImage(vkCtx, pyramidImageCommand);

        hiZImages.push_back(std::move(pyramidImage));
        hiZImageMemories.push_back(std::move(pyramidMemory));

        for (uint32_t i = 0; i < mipLevels; ++i) {
            hiZMipViews[f].push_back(VulkanUtils::createImageView(vkCtx, hiZImages[f], vk::Format::eR32Sfloat,
                                                                  vk::ImageAspectFlagBits::eColor, 1, i));
        }

        hiZFullViews.push_back(VulkanUtils::createImageView(vkCtx, hiZImages[f], vk::Format::eR32Sfloat,
                                                            vk::ImageAspectFlagBits::eColor, mipLevels, 0));
    }

    createDescriptorSets();
}

void OcclusionCuller::createDescriptorSetLayouts() {
    vk::DescriptorSetLayoutCreateInfo emptyLayoutInfo{};
    emptyLayoutInfo.bindingCount = 0;
    emptySetLayout = vk::raii::DescriptorSetLayout(vkCtx.device, emptyLayoutInfo);

    vk::DescriptorSetLayoutBinding depthBinding{};
    depthBinding.binding = 0;
    depthBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    depthBinding.descriptorCount = 1;
    depthBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutBinding mip0Binding{};
    mip0Binding.binding = 1;
    mip0Binding.descriptorType = vk::DescriptorType::eStorageImage;
    mip0Binding.descriptorCount = 1;
    mip0Binding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    const std::array mip0Bindings{depthBinding, mip0Binding};
    vk::DescriptorSetLayoutCreateInfo mip0LayoutInfo{};
    mip0LayoutInfo.bindingCount = static_cast<uint32_t>(mip0Bindings.size());
    mip0LayoutInfo.pBindings = mip0Bindings.data();
    mip0SetLayout = vk::raii::DescriptorSetLayout(vkCtx.device, mip0LayoutInfo);

    vk::DescriptorSetLayoutBinding srcBinding{};
    srcBinding.binding = 0;
    srcBinding.descriptorType = vk::DescriptorType::eStorageImage;
    srcBinding.descriptorCount = 1;
    srcBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutBinding dstBinding{};
    dstBinding.binding = 1;
    dstBinding.descriptorType = vk::DescriptorType::eStorageImage;
    dstBinding.descriptorCount = 1;
    dstBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    const std::array downsampleBindings{srcBinding, dstBinding};
    vk::DescriptorSetLayoutCreateInfo downsampleLayoutInfo{};
    downsampleLayoutInfo.bindingCount = static_cast<uint32_t>(downsampleBindings.size());
    downsampleLayoutInfo.pBindings = downsampleBindings.data();
    downsampleSetLayout = vk::raii::DescriptorSetLayout(vkCtx.device, downsampleLayoutInfo);

    vk::DescriptorSetLayoutBinding inputBinding{};
    inputBinding.binding = 0;
    inputBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    inputBinding.descriptorCount = 1;
    inputBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutBinding outputBinding{};
    outputBinding.binding = 1;
    outputBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    outputBinding.descriptorCount = 1;
    outputBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutBinding indirectBinding{};
    indirectBinding.binding = 2;
    indirectBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    indirectBinding.descriptorCount = 1;
    indirectBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutBinding eCombinedImageSampler{};
    eCombinedImageSampler.binding = 3;
    eCombinedImageSampler.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    eCombinedImageSampler.descriptorCount = 1;
    eCombinedImageSampler.stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutBinding eUniformBuffer{};
    eUniformBuffer.binding = 4;
    eUniformBuffer.descriptorType = vk::DescriptorType::eUniformBuffer;
    eUniformBuffer.descriptorCount = 1;
    eUniformBuffer.stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutBinding culledOutputBinding{};
    culledOutputBinding.binding = 6;
    culledOutputBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    culledOutputBinding.descriptorCount = 1;
    culledOutputBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutBinding culledIndirectBinding{};
    culledIndirectBinding.binding = 7;
    culledIndirectBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    culledIndirectBinding.descriptorCount = 1;
    culledIndirectBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    const std::array cullBindings{inputBinding,   outputBinding,       indirectBinding,      eCombinedImageSampler,
                                  eUniformBuffer, culledOutputBinding, culledIndirectBinding};
    vk::DescriptorSetLayoutCreateInfo cullLayoutInfo{};
    cullLayoutInfo.bindingCount = static_cast<uint32_t>(cullBindings.size());
    cullLayoutInfo.pBindings = cullBindings.data();
    cullSetLayout = vk::raii::DescriptorSetLayout(vkCtx.device, cullLayoutInfo);
}

void OcclusionCuller::createCullPipeline() {
    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushConstantRange.size = sizeof(PyramidPushConstants);

    cullPipelineLayout =
        VulkanUtils::createPipelineLayout(vkCtx, {*emptySetLayout, *emptySetLayout, *cullSetLayout}, {pushConstantRange});

    cullPipeline = ComputePipelineBuilder(vkCtx).build("shaders/cull/CullInstances.spv", *cullPipelineLayout);
}

void OcclusionCuller::createDescriptorSets() {
    const auto framesU = static_cast<uint32_t>(maxFramesInFlight);

    if (!poolCreated) {
        constexpr uint32_t MAX_POSSIBLE_MIP_LEVELS = 16;

        const std::array poolSizes{
            vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, framesU},
            vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, framesU * (1 + (MAX_POSSIBLE_MIP_LEVELS - 1) * 2)},
        };

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = framesU * MAX_POSSIBLE_MIP_LEVELS;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        descriptorPool = vk::raii::DescriptorPool(vkCtx.device, poolInfo);

        poolCreated = true;
    }

    mip0DescriptorSets.clear();
    downsampleDescriptorSets.clear();
    downsampleDescriptorSets.resize(maxFramesInFlight);

    DescriptorWriter writer(vkCtx);

    for (uint32_t f = 0; f < framesU; ++f) {
        vk::DescriptorSetAllocateInfo mip0AllocInfo{};
        mip0AllocInfo.descriptorPool = *descriptorPool;
        mip0AllocInfo.descriptorSetCount = 1;
        mip0AllocInfo.pSetLayouts = &*mip0SetLayout;
        mip0DescriptorSets.push_back(std::move(vk::raii::DescriptorSets(vkCtx.device, mip0AllocInfo).front()));

        writer
            .writeImage(*mip0DescriptorSets[f], 0, vk::DescriptorType::eCombinedImageSampler, *resolvedDepthImageViews[f],
                        *depthSampler)
            .writeImage(*mip0DescriptorSets[f], 1, vk::DescriptorType::eStorageImage, *hiZMipViews[f][0], nullptr,
                        vk::ImageLayout::eGeneral);

        for (uint32_t i = 1; i < mipLevels; ++i) {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = *descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &*downsampleSetLayout;
            vk::raii::DescriptorSet set = std::move(vk::raii::DescriptorSets(vkCtx.device, allocInfo).front());

            writer
                .writeImage(*set, 0, vk::DescriptorType::eStorageImage, *hiZMipViews[f][i - 1], nullptr,
                            vk::ImageLayout::eGeneral)
                .writeImage(*set, 1, vk::DescriptorType::eStorageImage, *hiZMipViews[f][i], nullptr, vk::ImageLayout::eGeneral);

            downsampleDescriptorSets[f].push_back(std::move(set));
        }
    }

    writer.update();
}

void OcclusionCuller::createPipelines() {
    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushConstantRange.size = sizeof(PyramidPushConstants);

    mip0PipelineLayout = VulkanUtils::createPipelineLayout(vkCtx, {*mip0SetLayout}, {pushConstantRange});
    downsamplePipelineLayout =
        VulkanUtils::createPipelineLayout(vkCtx, {*emptySetLayout, *downsampleSetLayout}, {pushConstantRange});

    const ComputePipelineBuilder builder(vkCtx);
    depthToMip0Pipeline = builder.build("shaders/cull/DepthToMip0.spv", *mip0PipelineLayout);
    downsamplePipeline = builder.build("shaders/cull/Downsample.spv", *downsamplePipelineLayout);
}

void OcclusionCuller::buildPyramid(const vk::raii::CommandBuffer &commandBuffer, const uint32_t frameIndex) const {
    using enum vk::PipelineStageFlagBits2;
    constexpr uint32_t GROUP = 8;

    VulkanUtils::ImageBarrierCommand depthCommand = {};
    depthCommand.image = *resolvedDepthImages[frameIndex];
    depthCommand.old_layout = vk::ImageLayout::eDepthAttachmentOptimal;
    depthCommand.new_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    depthCommand.src_access_mask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    depthCommand.dst_access_mask = vk::AccessFlagBits2::eShaderRead;
    depthCommand.src_stage_mask = eLateFragmentTests;
    depthCommand.dst_stage_mask = eComputeShader;
    depthCommand.image_aspect_flags = vk::ImageAspectFlagBits::eDepth;
    depthCommand.base_mip_level = 0;
    depthCommand.level_count = 1;

    VulkanUtils::ImageBarrierCommand hiZCommand = {};
    hiZCommand.image = *hiZImages[frameIndex];
    hiZCommand.old_layout = vk::ImageLayout::eUndefined;
    hiZCommand.new_layout = vk::ImageLayout::eGeneral;
    hiZCommand.src_access_mask = {};
    hiZCommand.dst_access_mask = vk::AccessFlagBits2::eShaderWrite;
    hiZCommand.src_stage_mask = eTopOfPipe;
    hiZCommand.dst_stage_mask = eComputeShader;
    hiZCommand.image_aspect_flags = vk::ImageAspectFlagBits::eColor;
    hiZCommand.base_mip_level = 0;
    hiZCommand.level_count = mipLevels;

    VulkanUtils::imageBarriers(commandBuffer, {depthCommand, hiZCommand});

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *depthToMip0Pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *mip0PipelineLayout, 0, *mip0DescriptorSets[frameIndex],
                                     nullptr);

    PyramidPushConstants pc0{{0, 0}, {static_cast<int32_t>(mipExtents[0].width), static_cast<int32_t>(mipExtents[0].height)}};
    commandBuffer.pushConstants<PyramidPushConstants>(*mip0PipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, pc0);
    commandBuffer.dispatch((mipExtents[0].width + GROUP - 1) / GROUP, (mipExtents[0].height + GROUP - 1) / GROUP, 1);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *downsamplePipeline);

    for (uint32_t i = 1; i < mipLevels; ++i) {
        VulkanUtils::ImageBarrierCommand hiZCommand2 = {};
        hiZCommand2.image = *hiZImages[frameIndex];
        hiZCommand2.old_layout = vk::ImageLayout::eGeneral;
        hiZCommand2.new_layout = vk::ImageLayout::eGeneral;
        hiZCommand2.src_access_mask = vk::AccessFlagBits2::eShaderWrite;
        hiZCommand2.dst_access_mask = vk::AccessFlagBits2::eShaderRead;
        hiZCommand2.src_stage_mask = eComputeShader;
        hiZCommand2.dst_stage_mask = eComputeShader;
        hiZCommand2.image_aspect_flags = vk::ImageAspectFlagBits::eColor;
        hiZCommand2.base_mip_level = i - 1;
        hiZCommand2.level_count = 1;

        VulkanUtils::imageBarriers(commandBuffer, {hiZCommand2});

        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *downsamplePipelineLayout, 1,
                                         *downsampleDescriptorSets[frameIndex][i - 1], nullptr);

        PyramidPushConstants pc{{static_cast<int32_t>(mipExtents[i - 1].width), static_cast<int32_t>(mipExtents[i - 1].height)},
                                {static_cast<int32_t>(mipExtents[i].width), static_cast<int32_t>(mipExtents[i].height)}};
        commandBuffer.pushConstants<PyramidPushConstants>(*downsamplePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, pc);
        commandBuffer.dispatch((mipExtents[i].width + GROUP - 1) / GROUP, (mipExtents[i].height + GROUP - 1) / GROUP, 1);
    }

    VulkanUtils::ImageBarrierCommand endHiZCommand = {};
    endHiZCommand.image = *hiZImages[frameIndex];
    endHiZCommand.old_layout = vk::ImageLayout::eGeneral;
    endHiZCommand.new_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    endHiZCommand.src_access_mask = vk::AccessFlagBits2::eShaderWrite;
    endHiZCommand.dst_access_mask = vk::AccessFlagBits2::eShaderRead;
    endHiZCommand.src_stage_mask = eComputeShader;
    endHiZCommand.dst_stage_mask = eComputeShader;
    endHiZCommand.image_aspect_flags = vk::ImageAspectFlagBits::eColor;
    endHiZCommand.base_mip_level = 0;
    endHiZCommand.level_count = mipLevels;

    VulkanUtils::imageBarriers(commandBuffer, {endHiZCommand});
}

void OcclusionCuller::prepareDepthResolveTarget(const vk::raii::CommandBuffer &commandBuffer, const uint32_t frameIndex) const {
    using enum vk::PipelineStageFlagBits2;

    VulkanUtils::ImageBarrierCommand depthCommand = {};
    depthCommand.image = *resolvedDepthImages[frameIndex];
    depthCommand.old_layout = vk::ImageLayout::eUndefined;
    depthCommand.new_layout = vk::ImageLayout::eDepthAttachmentOptimal;
    depthCommand.src_access_mask = {};
    depthCommand.dst_access_mask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
    depthCommand.src_stage_mask = eTopOfPipe;
    depthCommand.dst_stage_mask = eLateFragmentTests;
    depthCommand.image_aspect_flags = vk::ImageAspectFlagBits::eDepth;
    depthCommand.base_mip_level = 0;
    depthCommand.level_count = 1;

    VulkanUtils::imageBarriers(commandBuffer, {depthCommand});
}
