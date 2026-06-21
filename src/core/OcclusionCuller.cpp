#include "OcclusionCuller.hpp"
#include "VulkanUtils.hpp"
#include <array>
#include <cmath>

OcclusionCuller::OcclusionCuller(VulkanContext &context, const int maxFramesInFlight)
    : vkCtx(context), maxFramesInFlight(maxFramesInFlight) {}

void OcclusionCuller::createResources(const vk::Extent2D ext, const vk::Format depthFormat) {
    extent = ext;

    mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(extent.width, extent.height)))) + 1;
    mipExtents.clear();
    mipExtents.reserve(mipLevels);
    for (uint32_t i = 0; i < mipLevels; ++i) {
        mipExtents.push_back({std::max(1u, extent.width >> i), std::max(1u, extent.height >> i)});
    }

    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vk::Filter::eNearest;
    samplerInfo.minFilter = vk::Filter::eNearest;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    depthSampler = vk::raii::Sampler(vkCtx.device, samplerInfo);

    resolvedDepthImages.clear();
    resolvedDepthImageMemories.clear();
    resolvedDepthImageViews.clear();
    hiZImages.clear();
    hiZImageMemories.clear();
    hiZMipViews.clear();
    hiZMipViews.resize(maxFramesInFlight);

    for (int f = 0; f < maxFramesInFlight; ++f) {
        auto [depthImage, depthMemory] = VulkanUtils::createImage(
            vkCtx, {extent.width, extent.height, 1, vk::SampleCountFlagBits::e1, depthFormat, vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
                    vk::MemoryPropertyFlagBits::eDeviceLocal});
        vk::raii::ImageView depthView =
            VulkanUtils::createImageView(vkCtx, depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);

        resolvedDepthImages.push_back(std::move(depthImage));
        resolvedDepthImageMemories.push_back(std::move(depthMemory));
        resolvedDepthImageViews.push_back(std::move(depthView));

        auto [pyramidImage, pyramidMemory] = VulkanUtils::createImage(
            vkCtx, {extent.width, extent.height, mipLevels, vk::SampleCountFlagBits::e1, vk::Format::eR32Sfloat,
                    vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                    vk::MemoryPropertyFlagBits::eDeviceLocal});

        hiZImages.push_back(std::move(pyramidImage));
        hiZImageMemories.push_back(std::move(pyramidMemory));

        for (uint32_t i = 0; i < mipLevels; ++i) {
            hiZMipViews[f].push_back(VulkanUtils::createImageView(vkCtx, hiZImages[f], vk::Format::eR32Sfloat,
                                                                  vk::ImageAspectFlagBits::eColor, 1, i));
        }

        hiZFullViews.push_back(VulkanUtils::createImageView(vkCtx, hiZImages[f], vk::Format::eR32Sfloat,
                                                            vk::ImageAspectFlagBits::eColor, mipLevels, 0));
    }

    vk::SamplerCreateInfo hiZSamplerInfo{};
    hiZSamplerInfo.magFilter = vk::Filter::eNearest;
    hiZSamplerInfo.minFilter = vk::Filter::eNearest;
    hiZSamplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
    hiZSamplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    hiZSamplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    hiZSamplerInfo.minLod = 0.0f;
    hiZSamplerInfo.maxLod = static_cast<float>(mipLevels - 1);
    hiZSampler = vk::raii::Sampler(vkCtx.device, hiZSamplerInfo);

    createDescriptorSetLayouts();
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

void OcclusionCuller::createCullPipeline(const vk::PipelineShaderStageCreateInfo &cullStage) {
    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushConstantRange.size = sizeof(PyramidPushConstants);

    const std::array cullSetLayouts{*emptySetLayout, *emptySetLayout, *cullSetLayout};
    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = static_cast<uint32_t>(cullSetLayouts.size());
    layoutInfo.pSetLayouts = cullSetLayouts.data();
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    cullPipelineLayout = vk::raii::PipelineLayout(vkCtx.device, layoutInfo);

    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage = cullStage;
    pipelineInfo.layout = cullPipelineLayout;
    cullPipeline = vk::raii::Pipeline(vkCtx.device, nullptr, pipelineInfo);
}

void OcclusionCuller::createDescriptorSets() {
    const uint32_t downsampleCount = mipLevels - 1;
    const auto framesU = static_cast<uint32_t>(maxFramesInFlight);

    const std::array poolSizes{
        vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, framesU},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, framesU * (1 + downsampleCount * 2)},
    };

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = framesU * (1 + downsampleCount);
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    descriptorPool = vk::raii::DescriptorPool(vkCtx.device, poolInfo);

    mip0DescriptorSets.clear();
    downsampleDescriptorSets.clear();
    downsampleDescriptorSets.resize(maxFramesInFlight);

    for (int f = 0; f < maxFramesInFlight; ++f) {
        vk::DescriptorSetAllocateInfo mip0AllocInfo{};
        mip0AllocInfo.descriptorPool = *descriptorPool;
        mip0AllocInfo.descriptorSetCount = 1;
        mip0AllocInfo.pSetLayouts = &*mip0SetLayout;
        mip0DescriptorSets.push_back(std::move(vk::raii::DescriptorSets(vkCtx.device, mip0AllocInfo).front()));

        vk::DescriptorImageInfo depthImageInfo{};
        depthImageInfo.sampler = *depthSampler;
        depthImageInfo.imageView = *resolvedDepthImageViews[f];
        depthImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorImageInfo mip0ImageInfo{};
        mip0ImageInfo.imageView = *hiZMipViews[f][0];
        mip0ImageInfo.imageLayout = vk::ImageLayout::eGeneral;

        const std::array mip0Writes{
            vk::WriteDescriptorSet{*mip0DescriptorSets[f], 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &depthImageInfo},
            vk::WriteDescriptorSet{*mip0DescriptorSets[f], 1, 0, 1, vk::DescriptorType::eStorageImage, &mip0ImageInfo},
        };
        vkCtx.device.updateDescriptorSets(mip0Writes, {});

        for (uint32_t i = 1; i < mipLevels; ++i) {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = *descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &*downsampleSetLayout;
            vk::raii::DescriptorSet set = std::move(vk::raii::DescriptorSets(vkCtx.device, allocInfo).front());

            vk::DescriptorImageInfo srcInfo{};
            srcInfo.imageView = *hiZMipViews[f][i - 1];
            srcInfo.imageLayout = vk::ImageLayout::eGeneral;

            vk::DescriptorImageInfo dstInfo{};
            dstInfo.imageView = *hiZMipViews[f][i];
            dstInfo.imageLayout = vk::ImageLayout::eGeneral;

            const std::array writes{
                vk::WriteDescriptorSet{*set, 0, 0, 1, vk::DescriptorType::eStorageImage, &srcInfo},
                vk::WriteDescriptorSet{*set, 1, 0, 1, vk::DescriptorType::eStorageImage, &dstInfo},
            };
            vkCtx.device.updateDescriptorSets(writes, {});

            downsampleDescriptorSets[f].push_back(std::move(set));
        }
    }
}

void OcclusionCuller::createPipelines(const vk::PipelineShaderStageCreateInfo &depthToMip0Stage,
                                      const vk::PipelineShaderStageCreateInfo &downsampleStage) {
    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PyramidPushConstants);

    vk::PipelineLayoutCreateInfo mip0LayoutInfo{};
    mip0LayoutInfo.setLayoutCount = 1;
    mip0LayoutInfo.pSetLayouts = &*mip0SetLayout;
    mip0LayoutInfo.pushConstantRangeCount = 1;
    mip0LayoutInfo.pPushConstantRanges = &pushConstantRange;
    mip0PipelineLayout = vk::raii::PipelineLayout(vkCtx.device, mip0LayoutInfo);

    const std::array downsampleSetLayouts{*emptySetLayout, *downsampleSetLayout};

    vk::PipelineLayoutCreateInfo downsampleLayoutInfo{};
    downsampleLayoutInfo.setLayoutCount = static_cast<uint32_t>(downsampleSetLayouts.size());
    downsampleLayoutInfo.pSetLayouts = downsampleSetLayouts.data();
    downsampleLayoutInfo.pushConstantRangeCount = 1;
    downsampleLayoutInfo.pPushConstantRanges = &pushConstantRange;
    downsamplePipelineLayout = vk::raii::PipelineLayout(vkCtx.device, downsampleLayoutInfo);

    vk::ComputePipelineCreateInfo mip0PipelineInfo{};
    mip0PipelineInfo.stage = depthToMip0Stage;
    mip0PipelineInfo.layout = mip0PipelineLayout;
    depthToMip0Pipeline = vk::raii::Pipeline(vkCtx.device, nullptr, mip0PipelineInfo);

    vk::ComputePipelineCreateInfo downsamplePipelineInfo{};
    downsamplePipelineInfo.stage = downsampleStage;
    downsamplePipelineInfo.layout = downsamplePipelineLayout;
    downsamplePipeline = vk::raii::Pipeline(vkCtx.device, nullptr, downsamplePipelineInfo);
}

void OcclusionCuller::buildPyramid(const vk::raii::CommandBuffer &commandBuffer, const uint32_t frameIndex) {
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
        hiZCommand2.old_layout = vk::ImageLayout::eGeneral; // Mip levels are already eGeneral now
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

void OcclusionCuller::prepareDepthResolveTarget(const vk::raii::CommandBuffer &commandBuffer, const uint32_t frameIndex) {
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
