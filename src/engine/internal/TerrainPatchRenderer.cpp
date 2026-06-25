#include "TerrainPatchRenderer.hpp"
#include "DescriptorWriter.hpp"
#include "Exceptions.hpp"
#include "PipelineBuilder.hpp"
#include "VulkanUtils.hpp"
#include <algorithm>
#include <array>

namespace {

std::shared_ptr<Mesh> buildGridMesh(VulkanContext &vkCtx, const vk::raii::CommandPool &commandPool, const int resolution) {
    std::vector<Mesh::Vertex> vertices;
    vertices.reserve(static_cast<size_t>(resolution) * resolution);
    for (int y = 0; y < resolution; ++y) {
        for (int x = 0; x < resolution; ++x) {
            Mesh::Vertex vertex{};
            vertex.pos = glm::vec3(static_cast<float>(x), static_cast<float>(y), 0.0f);
            vertices.push_back(vertex);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(resolution - 1) * (resolution - 1) * 6);
    const auto vertexIndex = [resolution](const int x, const int y) { return static_cast<uint32_t>(y * resolution + x); };

    for (int y = 0; y < resolution - 1; ++y) {
        for (int x = 0; x < resolution - 1; ++x) {
            const uint32_t topLeft = vertexIndex(x, y);
            const uint32_t topRight = vertexIndex(x + 1, y);
            const uint32_t bottomLeft = vertexIndex(x, y + 1);
            const uint32_t bottomRight = vertexIndex(x + 1, y + 1);

            indices.push_back(topLeft);
            indices.push_back(topRight);
            indices.push_back(bottomLeft);

            indices.push_back(topRight);
            indices.push_back(bottomRight);
            indices.push_back(bottomLeft);
        }
    }

    auto mesh = std::make_shared<Mesh>(vkCtx);
    mesh->vertices = std::move(vertices);
    mesh->indices = std::move(indices);
    mesh->createGeometryBuffers(commandPool);
    return mesh;
}

} // namespace

TerrainPatchRenderer::TerrainPatchRenderer(VulkanContext &vkCtx, const int maxFramesInFlight)
    : vkCtx(vkCtx), maxFramesInFlight(maxFramesInFlight) {}

void TerrainPatchRenderer::createMaterialSetLayout() {
    vk::DescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutBinding shadowBinding{};
    shadowBinding.binding = 4;
    shadowBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    shadowBinding.descriptorCount = 1;
    shadowBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    const auto layerArrayBinding = [](const uint32_t binding) {
        vk::DescriptorSetLayoutBinding b{};
        b.binding = binding;
        b.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        b.descriptorCount = static_cast<uint32_t>(MAX_MATERIAL_LAYERS);
        b.stageFlags = vk::ShaderStageFlagBits::eFragment;
        return b;
    };

    const std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        uboBinding, shadowBinding, layerArrayBinding(8), layerArrayBinding(9), layerArrayBinding(10),
    };

    materialSetLayout = VulkanUtils::createDescriptorSetLayout(vkCtx, bindings);
}

void TerrainPatchRenderer::buildTierMesh(Tier &tier, const vk::raii::CommandPool &commandPool, const int resolution) const {
    using enum vk::BufferUsageFlagBits;
    using enum vk::MemoryPropertyFlagBits;

    tier.instanceBuffer =
        PerFrameBuffer(vkCtx, static_cast<uint32_t>(maxFramesInFlight), sizeof(TerrainPatchInstance) * MAX_PATCHES,
                       eVertexBuffer, eHostVisible | eHostCoherent);
    tier.mesh = buildGridMesh(vkCtx, commandPool, resolution);
    tier.gridDim = static_cast<float>(resolution - 1);
}

void TerrainPatchRenderer::createGridMeshes(const vk::raii::CommandPool &commandPool, const TerrainConfig &config) {
    const int coarseResolution = std::max(config.chunkResolution, 2);
    buildTierMesh(coarseTier, commandPool, coarseResolution);
    coarseTier.morphSnapStride = 2.0f;

    const int coarseGridDim = coarseResolution - 1;
    const int requestedFineResolution =
        config.fineChunkResolution > 0 ? std::max(config.fineChunkResolution, 2) : coarseResolution;
    const int requestedFineGridDim = std::max(requestedFineResolution - 1, coarseGridDim);
    const int ratio = std::max(1, (requestedFineGridDim + coarseGridDim - 1) / coarseGridDim);
    const int fineGridDim = coarseGridDim * ratio;

    buildTierMesh(fineTier, commandPool, fineGridDim + 1);
    fineTier.morphSnapStride = 2.0f * static_cast<float>(ratio);

    pushConstants.noiseOffset = config.noise.offset;
    pushConstants.noiseScale = config.noise.scale;
    pushConstants.heightScale = config.noise.heightScale;
    pushConstants.baseHeight = config.noise.baseHeight;
    pushConstants.persistence = config.noise.persistence;
    pushConstants.lacunarity = config.noise.lacunarity;
    pushConstants.octaves = config.noise.octaves;
    pushConstants.textureWorldScale = config.textureWorldScale;
    pushConstants.uvScaleX = config.uvScale.x;
    pushConstants.uvScaleY = config.uvScale.y;
    pushConstants.ridgeSharpness = config.noise.ridgeSharpness;
    pushConstants.heightRedistribution = config.noise.heightRedistribution;
    pushConstants.regionScale = config.noise.regionScale;
    pushConstants.regionThreshold = config.noise.regionThreshold;
    pushConstants.regionBlendWidth = config.noise.regionBlendWidth;
    pushConstants.flatScale = config.noise.flatScale;
    pushConstants.flatThreshold = config.noise.flatThreshold;
    pushConstants.flatBlendWidth = std::max(config.noise.flatBlendWidth, 0.001f);
    pushConstants.minRelief = config.noise.minRelief;

    const auto layerOrDefault = [&config](const size_t index) -> TerrainMaterialLayer {
        return index < config.materialLayers.size() ? config.materialLayers[index] : TerrainMaterialLayer{};
    };
    const TerrainMaterialLayer layer0 = layerOrDefault(0);
    const TerrainMaterialLayer layer1 = layerOrDefault(1);
    const TerrainMaterialLayer layer2 = layerOrDefault(2);
    const TerrainMaterialLayer layer3 = layerOrDefault(3);

    pushConstants.layerCount = static_cast<int>(std::min(config.materialLayers.size(), MAX_MATERIAL_LAYERS));
    pushConstants.layer0PreferredHeight = layer0.preferredHeight;
    pushConstants.layer0HeightRange = std::max(layer0.heightRange, 0.001f);
    pushConstants.layer0PreferredSlope = layer0.preferredSlope;
    pushConstants.layer0SlopeRange = std::max(layer0.slopeRange, 0.001f);
    pushConstants.layer1PreferredHeight = layer1.preferredHeight;
    pushConstants.layer1HeightRange = std::max(layer1.heightRange, 0.001f);
    pushConstants.layer1PreferredSlope = layer1.preferredSlope;
    pushConstants.layer1SlopeRange = std::max(layer1.slopeRange, 0.001f);
    pushConstants.layer2PreferredHeight = layer2.preferredHeight;
    pushConstants.layer2HeightRange = std::max(layer2.heightRange, 0.001f);
    pushConstants.layer2PreferredSlope = layer2.preferredSlope;
    pushConstants.layer2SlopeRange = std::max(layer2.slopeRange, 0.001f);
    pushConstants.layer3PreferredHeight = layer3.preferredHeight;
    pushConstants.layer3HeightRange = std::max(layer3.heightRange, 0.001f);
    pushConstants.layer3PreferredSlope = layer3.preferredSlope;
    pushConstants.layer3SlopeRange = std::max(layer3.slopeRange, 0.001f);
}

void TerrainPatchRenderer::setMaterialLayers(const std::vector<TerrainMaterialLayer> &layers,
                                             const std::vector<vk::Buffer> &cameraUniformBuffers,
                                             const vk::ImageView shadowMapView, const vk::Sampler shadowSampler) {
    if (layers.empty()) {
        throw EngineExceptions::Compatibility("TerrainConfig.materialLayers must contain at least one layer");
    }
    if (layers.size() > MAX_MATERIAL_LAYERS) {
        throw EngineExceptions::Compatibility("TerrainConfig.materialLayers supports at most 4 layers");
    }

    if (materialDescriptorPool == nullptr) {
        const auto framesU = static_cast<uint32_t>(maxFramesInFlight);
        const std::array<vk::DescriptorPoolSize, 2> poolSizes = {
            vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, framesU},
            vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler,
                                   framesU * static_cast<uint32_t>(1 + MAX_MATERIAL_LAYERS * 3)},
        };

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = framesU;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        materialDescriptorPool = vk::raii::DescriptorPool(vkCtx.device, poolInfo);

        const std::vector layouts(maxFramesInFlight, *materialSetLayout);
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = *materialDescriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();
        materialSets = vkCtx.device.allocateDescriptorSets(allocInfo);
    }

    const auto materialForSlot = [&layers](const size_t index) -> const Material & {
        return index < layers.size() ? layers[index].material : layers.back().material;
    };

    DescriptorWriter writer(vkCtx);
    for (int f = 0; f < maxFramesInFlight; ++f) {
        writer.writeBuffer(*materialSets[f], 0, cameraUniformBuffers[f], vk::WholeSize, vk::DescriptorType::eUniformBuffer);
        writer.writeImage(*materialSets[f], 4, vk::DescriptorType::eCombinedImageSampler, shadowMapView, shadowSampler);

        for (uint32_t i = 0; i < static_cast<uint32_t>(MAX_MATERIAL_LAYERS); ++i) {
            const Material &material = materialForSlot(i);
            writer.writeImage(*materialSets[f], 8, vk::DescriptorType::eCombinedImageSampler,
                              *material.albedo->textureImageView, *material.albedo->textureSampler,
                              vk::ImageLayout::eShaderReadOnlyOptimal, i);
            writer.writeImage(*materialSets[f], 9, vk::DescriptorType::eCombinedImageSampler,
                              *material.normalMap->textureImageView, *material.normalMap->textureSampler,
                              vk::ImageLayout::eShaderReadOnlyOptimal, i);
            writer.writeImage(*materialSets[f], 10, vk::DescriptorType::eCombinedImageSampler,
                              *material.ormMap->textureImageView, *material.ormMap->textureSampler,
                              vk::ImageLayout::eShaderReadOnlyOptimal, i);
        }
    }
    writer.update();
}

void TerrainPatchRenderer::createPipelines(const vk::Format colorFormat, const vk::Format depthFormat,
                                           const vk::Format shadowDepthFormat) {
    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    pushConstantRange.size = sizeof(TerrainPushConstants);

    pipelineLayout = VulkanUtils::createPipelineLayout(vkCtx, {*materialSetLayout}, {pushConstantRange});

    const auto attributeDescriptions = Mesh::Vertex::getAttributeDescriptions();
    const vk::VertexInputBindingDescription bindingDescription = Mesh::Vertex::getBindingDescription();

    constexpr vk::VertexInputBindingDescription instanceBinding{1, sizeof(TerrainPatchInstance),
                                                                vk::VertexInputRate::eInstance};
    constexpr vk::VertexInputAttributeDescription instanceOriginAttr{5, 1, vk::Format::eR32G32Sfloat,
                                                                     offsetof(TerrainPatchInstance, origin)};
    constexpr vk::VertexInputAttributeDescription instanceParamsAttr{6, 1, vk::Format::eR32G32B32A32Sfloat,
                                                                     offsetof(TerrainPatchInstance, params)};

    const std::vector<vk::VertexInputAttributeDescription> patchAttrs = {attributeDescriptions[0], instanceOriginAttr,
                                                                         instanceParamsAttr};

    GraphicsPipelineBuilder builder(vkCtx);
    builder.addShaderStage(vk::ShaderStageFlagBits::eVertex, "shaders/shader.spv", "vertTerrain")
        .addShaderStage(vk::ShaderStageFlagBits::eFragment, "shaders/shader.spv", "fragTerrain")
        .setVertexInput({bindingDescription, instanceBinding}, patchAttrs)
        .setLayout(*pipelineLayout)
        .setColorFormats({colorFormat})
        .setDepthFormat(depthFormat)
        .setMSAA(vkCtx.msaaSamples);

    solidPipeline = builder.build();

    builder.setPolygonMode(vk::PolygonMode::eLine);
    wireframePipeline = builder.build();

    GraphicsPipelineBuilder shadowBuilder(vkCtx);
    shadowBuilder.addShaderStage(vk::ShaderStageFlagBits::eVertex, "shaders/shader.spv", "vertTerrainShadow")
        .addShaderStage(vk::ShaderStageFlagBits::eFragment, "shaders/shader.spv", "fragTerrainShadow")
        .setVertexInput({bindingDescription, instanceBinding}, patchAttrs)
        .setLayout(*pipelineLayout)
        .setColorFormats({})
        .setDepthFormat(shadowDepthFormat)
        .setDepthBias(1.5f, 1.75f);

    shadowPipeline = shadowBuilder.build();
}

void TerrainPatchRenderer::setPatches(std::vector<TerrainPatchInstance> coarsePatches,
                                      std::vector<TerrainPatchInstance> finePatches) {
    coarseTier.pendingPatches = std::move(coarsePatches);
    fineTier.pendingPatches = std::move(finePatches);
}

void TerrainPatchRenderer::uploadTier(Tier &tier, const uint32_t frameIndex) {
    tier.activeInstanceCount = static_cast<uint32_t>(std::min(tier.pendingPatches.size(), MAX_PATCHES));
    if (tier.activeInstanceCount == 0) {
        return;
    }
    std::memcpy(tier.instanceBuffer.mapped(frameIndex), tier.pendingPatches.data(),
                sizeof(TerrainPatchInstance) * tier.activeInstanceCount);
}

void TerrainPatchRenderer::upload(const uint32_t frameIndex) {
    uploadTier(coarseTier, frameIndex);
    uploadTier(fineTier, frameIndex);
}

void TerrainPatchRenderer::drawTier(const Tier &tier, const vk::Pipeline pipeline, const DrawCommand &command) const {
    if (tier.activeInstanceCount == 0) {
        return;
    }

    TerrainPushConstants pc = pushConstants;
    pc.gridDim = tier.gridDim;
    pc.morphSnapStride = tier.morphSnapStride;

    command.commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    command.commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0,
                                              *materialSets[command.frameIndex], nullptr);
    command.commandBuffer->pushConstants<TerrainPushConstants>(
        *pipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, pc);

    const std::vector<vk::DeviceSize> vertexOffsets = {0};
    command.commandBuffer->bindVertexBuffers(0, *tier.mesh->unifiedBuffer, vertexOffsets);
    const std::vector<vk::DeviceSize> instanceOffsets = {0};
    command.commandBuffer->bindVertexBuffers(1, tier.instanceBuffer[command.frameIndex], instanceOffsets);
    const vk::DeviceSize vertexSizeOffset = sizeof(Mesh::Vertex) * tier.mesh->vertices.size();
    command.commandBuffer->bindIndexBuffer(*tier.mesh->unifiedBuffer, vertexSizeOffset, vk::IndexType::eUint32);

    command.commandBuffer->drawIndexed(static_cast<uint32_t>(tier.mesh->indices.size()), tier.activeInstanceCount, 0, 0, 0);
}

void TerrainPatchRenderer::draw(const DrawCommand &command, const bool isWireframe, uint32_t &drawCallCount) const {
    drawTier(coarseTier, isWireframe ? *wireframePipeline : *solidPipeline, command);
    if (coarseTier.activeInstanceCount > 0) {
        drawCallCount++;
    }

    drawTier(fineTier, isWireframe ? *wireframePipeline : *solidPipeline, command);
    if (fineTier.activeInstanceCount > 0) {
        drawCallCount++;
    }
}

void TerrainPatchRenderer::drawShadow(const DrawCommand &command) const {
    drawTier(coarseTier, *shadowPipeline, command);
    drawTier(fineTier, *shadowPipeline, command);
}

uint64_t TerrainPatchRenderer::getVisibleVertexEstimate() const {
    uint64_t total = 0;

    if (coarseTier.mesh) {
        total += static_cast<uint64_t>(coarseTier.mesh->indices.size()) * coarseTier.activeInstanceCount;
    }

    if (fineTier.mesh) {
        total += static_cast<uint64_t>(fineTier.mesh->indices.size()) * fineTier.activeInstanceCount;
    }

    return total;
}
