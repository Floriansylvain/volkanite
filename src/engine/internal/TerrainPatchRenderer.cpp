#include "TerrainPatchRenderer.hpp"
#include "DescriptorWriter.hpp"
#include "Exceptions.hpp"
#include "PipelineBuilder.hpp"
#include "VulkanUtils.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_map>

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

    const auto vertexIndex = [resolution](const int x, const int y) { return static_cast<uint32_t>(y * resolution + x); };

    std::unordered_map<int, uint32_t> skirtIndexForCell;
    const auto addSkirtVertex = [&](const int x, const int y) -> uint32_t {
        const int key = y * resolution + x;
        if (const auto it = skirtIndexForCell.find(key); it != skirtIndexForCell.end()) {
            return it->second;
        }
        Mesh::Vertex skirt{};
        skirt.pos = glm::vec3(static_cast<float>(x), static_cast<float>(y), 1.0f);
        const auto index = static_cast<uint32_t>(vertices.size());
        vertices.push_back(skirt);
        skirtIndexForCell.emplace(key, index);
        return index;
    };

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(resolution - 1) * (resolution - 1) * 6 + static_cast<size_t>(resolution - 1) * 4 * 12);

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

    const auto addSkirtQuad = [&](const int ax, const int ay, const int bx, const int by) {
        const uint32_t outerA = vertexIndex(ax, ay);
        const uint32_t outerB = vertexIndex(bx, by);
        const uint32_t skirtA = addSkirtVertex(ax, ay);
        const uint32_t skirtB = addSkirtVertex(bx, by);

        indices.push_back(outerA);
        indices.push_back(outerB);
        indices.push_back(skirtB);
        indices.push_back(outerA);
        indices.push_back(skirtB);
        indices.push_back(skirtA);
        indices.push_back(outerA);
        indices.push_back(skirtB);
        indices.push_back(outerB);
        indices.push_back(outerA);
        indices.push_back(skirtA);
        indices.push_back(skirtB);
    };

    for (int x = 0; x < resolution - 1; ++x) {
        addSkirtQuad(x, 0, x + 1, 0);
        addSkirtQuad(x, resolution - 1, x + 1, resolution - 1);
    }
    for (int y = 0; y < resolution - 1; ++y) {
        addSkirtQuad(0, y, 0, y + 1);
        addSkirtQuad(resolution - 1, y, resolution - 1, y + 1);
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

// ---------------------------------------------------------------------------
//   Binding 0  – camera UBO          (vertex + fragment)
//   Binding 4  – shadow sampler      (fragment)
//   Binding 5  – biome layer SSBO    (fragment)   <-- new
//   Binding 8  – albedo array        (fragment, MAX_BIOME_LAYERS entries)
//   Binding 9  – normal-map array    (fragment, MAX_BIOME_LAYERS entries)
//   Binding 10 – ORM array           (fragment, MAX_BIOME_LAYERS entries)
// ---------------------------------------------------------------------------
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

    vk::DescriptorSetLayoutBinding biomeSSBOBinding{};
    biomeSSBOBinding.binding = 5;
    biomeSSBOBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    biomeSSBOBinding.descriptorCount = 1;
    biomeSSBOBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    const auto layerArrayBinding = [](const uint32_t binding) {
        vk::DescriptorSetLayoutBinding b{};
        b.binding = binding;
        b.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        b.descriptorCount = static_cast<uint32_t>(MAX_BIOME_LAYERS);
        b.stageFlags = vk::ShaderStageFlagBits::eFragment;
        return b;
    };

    const std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        uboBinding, shadowBinding, biomeSSBOBinding, layerArrayBinding(8), layerArrayBinding(9), layerArrayBinding(10),
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

    // ------------------------------------------------------------------
    // Populate static push-constant fields from config.
    // Per-layer data is no longer in push constants; it lives in the SSBO.
    // ------------------------------------------------------------------
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

    // Moisture noise params
    pushConstants.moistureOffset = config.moistureOffset;
    pushConstants.moistureScale = config.moistureScale;

    pushConstants.layerCount = static_cast<int>(std::min(config.biomeLayers.size(), MAX_BIOME_LAYERS));
}

// ---------------------------------------------------------------------------
// setBiomeLayers – called once from Engine::createTerrain.
//
// Builds and uploads the per-frame GpuBiomeLayer SSBO and writes all
// descriptor sets (camera UBO, shadow, SSBO, albedo/normal/ORM arrays).
// ---------------------------------------------------------------------------
void TerrainPatchRenderer::setBiomeLayers(const std::vector<TerrainBiomeLayer> &layers,
                                          const std::vector<vk::Buffer> &cameraUniformBuffers,
                                          const vk::ImageView shadowMapView, const vk::Sampler shadowSampler) {
    if (layers.empty()) {
        throw EngineExceptions::Compatibility("TerrainConfig.biomeLayers must contain at least one layer");
    }
    if (layers.size() > MAX_BIOME_LAYERS) {
        throw EngineExceptions::Compatibility("TerrainConfig.biomeLayers exceeds MAX_BIOME_LAYERS (16)");
    }

    const auto count = static_cast<uint32_t>(layers.size());
    const auto framesU = static_cast<uint32_t>(maxFramesInFlight);

    std::vector<GpuBiomeLayer> gpuLayers(count);
    for (uint32_t i = 0; i < count; ++i) {
        const TerrainBiomeLayer &l = layers[i];
        gpuLayers[i].preferredHeight = l.preferredHeight;
        gpuLayers[i].heightRange = std::max(l.heightRange, 0.001f);
        gpuLayers[i].preferredSlope = l.preferredSlope;
        gpuLayers[i].slopeRange = std::max(l.slopeRange, 0.001f);
        gpuLayers[i].preferredMoisture = l.preferredMoisture;
        gpuLayers[i].moistureRange = std::max(l.moistureRange, 0.001f);
        gpuLayers[i].textureScale = std::max(l.textureScale, 0.001f);
        gpuLayers[i].patchiness = std::clamp(l.patchiness, 0.0f, 1.0f);
        gpuLayers[i].patchScale = std::max(l.patchScale, 1.0f);
        gpuLayers[i].blendSharpness = std::max(l.blendSharpness, 0.01f);
        gpuLayers[i].pad0 = 0.0f;
        gpuLayers[i].pad1 = 0.0f;
    }
    const vk::DeviceSize ssboSize = sizeof(GpuBiomeLayer) * MAX_BIOME_LAYERS;

    if (!biomeLayerBufferReady) {
        using enum vk::BufferUsageFlagBits;
        using enum vk::MemoryPropertyFlagBits;
        biomeLayerBuffer = PerFrameBuffer(vkCtx, framesU, ssboSize, eStorageBuffer, eHostVisible | eHostCoherent);
        biomeLayerBufferReady = true;
    }

    // Upload the same data to every frame slot
    const size_t uploadBytes = sizeof(GpuBiomeLayer) * count;
    for (int f = 0; f < maxFramesInFlight; ++f) {
        std::memcpy(biomeLayerBuffer.mapped(f), gpuLayers.data(), uploadBytes);
    }

    if (materialDescriptorPool == nullptr) {
        const std::array<vk::DescriptorPoolSize, 3> poolSizes = {
            vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, framesU},
            vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, framesU},
            vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler,
                                   framesU * (1u + static_cast<uint32_t>(MAX_BIOME_LAYERS) * 3u)},
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

        writer.writeBuffer(*materialSets[f], 5, biomeLayerBuffer[f], ssboSize, vk::DescriptorType::eStorageBuffer);

        for (uint32_t i = 0; i < static_cast<uint32_t>(MAX_BIOME_LAYERS); ++i) {
            const Material &mat = materialForSlot(i);
            writer.writeImage(*materialSets[f], 8, vk::DescriptorType::eCombinedImageSampler, *mat.albedo->textureImageView,
                              *mat.albedo->textureSampler, vk::ImageLayout::eShaderReadOnlyOptimal, i);
            writer.writeImage(*materialSets[f], 9, vk::DescriptorType::eCombinedImageSampler, *mat.normalMap->textureImageView,
                              *mat.normalMap->textureSampler, vk::ImageLayout::eShaderReadOnlyOptimal, i);
            writer.writeImage(*materialSets[f], 10, vk::DescriptorType::eCombinedImageSampler, *mat.ormMap->textureImageView,
                              *mat.ormMap->textureSampler, vk::ImageLayout::eShaderReadOnlyOptimal, i);
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

void TerrainPatchRenderer::setViewBias(const glm::vec3 &cameraForward, const TerrainConfig &config) {
    glm::vec2 forward2D(cameraForward.x, cameraForward.y);
    if (glm::dot(forward2D, forward2D) < 1e-8f) {
        forward2D = glm::vec2(1.0f, 0.0f);
    } else {
        forward2D = glm::normalize(forward2D);
    }

    pushConstants.useViewBias = config.useViewBias ? 1 : 0;
    pushConstants.viewFullResRadius = config.viewFullResRadius;
    pushConstants.viewAheadMultiplier = config.viewAheadMultiplier;
    pushConstants.viewBehindMultiplier = config.viewBehindMultiplier;
    pushConstants.cameraForwardX = forward2D.x;
    pushConstants.cameraForwardY = forward2D.y;
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
    const std::vector<vk::DeviceSize> instanceOffsets = {0};
    command.commandBuffer->bindVertexBuffers(0, *tier.mesh->unifiedBuffer, vertexOffsets);
    command.commandBuffer->bindVertexBuffers(1, tier.instanceBuffer[command.frameIndex], instanceOffsets);
    const vk::DeviceSize indexOffset = sizeof(Mesh::Vertex) * tier.mesh->vertices.size();
    command.commandBuffer->bindIndexBuffer(*tier.mesh->unifiedBuffer, indexOffset, vk::IndexType::eUint32);

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
