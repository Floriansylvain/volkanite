#include "TerrainPatchRenderer.hpp"
#include "DescriptorWriter.hpp"
#include "Exceptions.hpp"
#include "PipelineBuilder.hpp"
#include "VulkanUtils.hpp"
#include <algorithm>
#include <array>
#include <cstring>

namespace {

std::shared_ptr<Mesh> buildGridMeshWithSkirt(VulkanContext &vkCtx, const vk::raii::CommandPool &commandPool,
                                             const int resolution) {
    std::vector<Mesh::Vertex> vertices;
    vertices.reserve(static_cast<size_t>(resolution) * resolution + static_cast<size_t>(resolution) * 4);
    for (int y = 0; y < resolution; ++y) {
        for (int x = 0; x < resolution; ++x) {
            Mesh::Vertex vertex{};
            vertex.pos = glm::vec3(static_cast<float>(x), static_cast<float>(y), 0.0f);
            vertices.push_back(vertex);
        }
    }

    const auto vertexIndex = [resolution](const int x, const int y) { return static_cast<uint32_t>(y * resolution + x); };

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(resolution - 1) * (resolution - 1) * 6);
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

    const auto addSkirtEdge = [&](const auto &edgeVertexAt, const int count) {
        const auto skirtBase = static_cast<uint32_t>(vertices.size());
        for (int i = 0; i < count; ++i) {
            Mesh::Vertex skirtVertex = vertices[edgeVertexAt(i)];
            skirtVertex.pos.z = 1.0f;
            vertices.push_back(skirtVertex);
        }

        for (int i = 0; i < count - 1; ++i) {
            const uint32_t a = edgeVertexAt(i);
            const uint32_t b = edgeVertexAt(i + 1);
            const uint32_t sa = skirtBase + static_cast<uint32_t>(i);
            const uint32_t sb = skirtBase + static_cast<uint32_t>(i) + 1;

            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(sa);
            indices.push_back(b);
            indices.push_back(sb);
            indices.push_back(sa);

            indices.push_back(a);
            indices.push_back(sa);
            indices.push_back(b);
            indices.push_back(b);
            indices.push_back(sa);
            indices.push_back(sb);
        }
    };

    addSkirtEdge([&](const int i) { return vertexIndex(i, 0); }, resolution);
    addSkirtEdge([&](const int i) { return vertexIndex(i, resolution - 1); }, resolution);
    addSkirtEdge([&](const int i) { return vertexIndex(0, i); }, resolution);
    addSkirtEdge([&](const int i) { return vertexIndex(resolution - 1, i); }, resolution);

    auto mesh = std::make_shared<Mesh>(vkCtx);
    mesh->vertices = std::move(vertices);
    mesh->indices = std::move(indices);
    mesh->createGeometryBuffers(commandPool);
    return mesh;
}

} // namespace

TerrainPatchRenderer::TerrainPatchRenderer(VulkanContext &vkCtx, const int maxFramesInFlight)
    : vkCtx(vkCtx), maxFramesInFlight(maxFramesInFlight), computeBaker(vkCtx) {}

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

    vk::DescriptorSetLayoutBinding layerParamsBinding{};
    layerParamsBinding.binding = 11;
    layerParamsBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    layerParamsBinding.descriptorCount = 1;
    layerParamsBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

    const std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        uboBinding, shadowBinding, layerArrayBinding(8), layerArrayBinding(9), layerArrayBinding(10), layerParamsBinding,
    };

    materialSetLayout = VulkanUtils::createDescriptorSetLayout(vkCtx, bindings);

    vk::DescriptorSetLayoutBinding heightBinding{};
    heightBinding.binding = 0;
    heightBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    heightBinding.descriptorCount = 1;
    heightBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

    vk::DescriptorSetLayoutBinding normalBinding{};
    normalBinding.binding = 1;
    normalBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    normalBinding.descriptorCount = 1;
    normalBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

    chunkSetLayout = VulkanUtils::createDescriptorSetLayout(vkCtx, {heightBinding, normalBinding});
}

void TerrainPatchRenderer::createChunkMeshes(const vk::raii::CommandPool &commandPool, const TerrainConfig &config) {
    if (config.lodLevels.empty()) {
        throw EngineExceptions::Compatibility("TerrainConfig.lodLevels must contain at least one level");
    }

    lodMeshes.clear();
    lodMeshes.reserve(config.lodLevels.size());
    for (const auto &lod : config.lodLevels) {
        const int resolution = std::max(lod.meshResolution, 2);
        LodMesh lodMesh;
        lodMesh.mesh = buildGridMeshWithSkirt(vkCtx, commandPool, resolution);
        lodMesh.gridDim = static_cast<float>(resolution - 1);
        lodMeshes.push_back(std::move(lodMesh));
    }

    pushConstants.textureWorldScale = config.textureWorldScale;
    pushConstants.uvScaleX = config.uvScale.x;
    pushConstants.uvScaleY = config.uvScale.y;
    pushConstants.skirtDepth = config.lodSkirtDepth;

    verticalCullExtent = config.noise.heightScale * 3.0f;

    lodLevels = config.lodLevels;
    noiseSettings = config.noise;

    int maxHeightmapResolution = 2;
    for (const auto &lod : config.lodLevels) {
        maxHeightmapResolution = std::max(maxHeightmapResolution, lod.heightmapResolution);
    }
    const int expectedConcurrentBakes = std::max(config.maxChunkUploadsPerFrame, 1) * (maxFramesInFlight + 2);
    computeBaker.createResources(maxHeightmapResolution + 2, expectedConcurrentBakes);
    computeBaker.createPipelines();

    const uint32_t radius = static_cast<uint32_t>(std::max(config.renderDistanceChunks, 0)) + 2;
    chunkDescriptorPoolCapacity = (2 * radius + 1) * (2 * radius + 1) * 2;
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
        const std::array<vk::DescriptorPoolSize, 3> poolSizes = {
            vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, framesU},
            vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, framesU},
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

    using enum vk::BufferUsageFlagBits;
    using enum vk::MemoryPropertyFlagBits;
    layerParamsBuffer =
        PerFrameBuffer(vkCtx, static_cast<uint32_t>(maxFramesInFlight), sizeof(TerrainLayerParamsGpu) * MAX_MATERIAL_LAYERS,
                       eStorageBuffer, eHostVisible | eHostCoherent);

    std::array<TerrainLayerParamsGpu, MAX_MATERIAL_LAYERS> layerParams{};
    for (size_t i = 0; i < layers.size(); ++i) {
        layerParams[i].preferredHeight = layers[i].preferredHeight;
        layerParams[i].heightRange = std::max(layers[i].heightRange, 0.001f);
        layerParams[i].preferredSlope = layers[i].preferredSlope;
        layerParams[i].slopeRange = std::max(layers[i].slopeRange, 0.001f);
    }

    const auto materialForSlot = [&layers](const size_t index) -> const Material & {
        return index < layers.size() ? layers[index].material : layers.back().material;
    };

    DescriptorWriter writer(vkCtx);
    for (int f = 0; f < maxFramesInFlight; ++f) {
        std::memcpy(layerParamsBuffer.mapped(f), layerParams.data(), sizeof(TerrainLayerParamsGpu) * MAX_MATERIAL_LAYERS);

        writer.writeBuffer(*materialSets[f], 0, cameraUniformBuffers[f], vk::WholeSize, vk::DescriptorType::eUniformBuffer);
        writer.writeImage(*materialSets[f], 4, vk::DescriptorType::eCombinedImageSampler, shadowMapView, shadowSampler);
        writer.writeBuffer(*materialSets[f], 11, layerParamsBuffer[f], vk::WholeSize, vk::DescriptorType::eStorageBuffer);

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

    pushConstants.layerCount = static_cast<int>(layers.size());
}

void TerrainPatchRenderer::createPipelines(const vk::Format colorFormat, const vk::Format depthFormat,
                                           const vk::Format shadowDepthFormat) {
    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    pushConstantRange.size = sizeof(TerrainPushConstants);

    pipelineLayout = VulkanUtils::createPipelineLayout(vkCtx, {*materialSetLayout, *chunkSetLayout}, {pushConstantRange});

    const auto attributeDescriptions = Mesh::Vertex::getAttributeDescriptions();
    const vk::VertexInputBindingDescription bindingDescription = Mesh::Vertex::getBindingDescription();
    const std::vector<vk::VertexInputAttributeDescription> patchAttrs = {attributeDescriptions[0]};

    GraphicsPipelineBuilder builder(vkCtx);
    builder.addShaderStage(vk::ShaderStageFlagBits::eVertex, "shaders/shader.spv", "vertTerrain")
        .addShaderStage(vk::ShaderStageFlagBits::eFragment, "shaders/shader.spv", "fragTerrain")
        .setVertexInput({bindingDescription}, patchAttrs)
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
        .setVertexInput({bindingDescription}, patchAttrs)
        .setLayout(*pipelineLayout)
        .setColorFormats({})
        .setDepthFormat(shadowDepthFormat)
        .setDepthBias(1.5f, 1.75f);

    shadowPipeline = shadowBuilder.build();
}

void TerrainPatchRenderer::addChunk(const TerrainChunkCoord coord, const glm::vec2 worldOrigin, const float worldSize,
                                    const int lodIndex, const vk::raii::CommandBuffer &commandBuffer) {
    if (chunkDescriptorPool == nullptr) {
        const std::array<vk::DescriptorPoolSize, 1> poolSizes = {
            vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, chunkDescriptorPoolCapacity * 2},
        };

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = chunkDescriptorPoolCapacity;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        chunkDescriptorPool = vk::raii::DescriptorPool(vkCtx.device, poolInfo);
    }

    const int resolution = std::max(lodLevels[lodIndex].heightmapResolution, 2);
    const float texelWorldSize = worldSize / static_cast<float>(resolution - 1);

    ChunkGpuData chunk;
    chunk.worldOrigin = worldOrigin;
    chunk.worldSize = worldSize;
    chunk.lodIndex = lodIndex;

    chunk.heightMap = std::make_shared<Texture>(vkCtx);
    chunk.heightMap->createStorageTarget(resolution, resolution, vk::Format::eR32Sfloat, commandBuffer);

    chunk.normalMap = std::make_shared<Texture>(vkCtx);
    chunk.normalMap->createStorageTarget(resolution, resolution, vk::Format::eR8G8B8A8Unorm, commandBuffer);

    TerrainComputeBaker::BakeCommand bakeCommand{};
    bakeCommand.commandBuffer = &commandBuffer;
    bakeCommand.chunkMin = worldOrigin - glm::vec2(worldSize * 0.5f) - glm::vec2(texelWorldSize);
    bakeCommand.texelWorldSize = texelWorldSize;
    bakeCommand.resolution = resolution;
    bakeCommand.noise = noiseSettings;
    bakeCommand.outHeightView = *chunk.heightMap->textureImageView;
    bakeCommand.outHeightImage = chunk.heightMap->getImage();
    bakeCommand.outNormalView = *chunk.normalMap->textureImageView;
    bakeCommand.outNormalImage = chunk.normalMap->getImage();
    pendingBakeResources.push_back(PendingBakeResources{computeBaker.dispatchBake(bakeCommand), maxFramesInFlight});

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = *chunkDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &*chunkSetLayout;
    std::vector<vk::raii::DescriptorSet> sets = vkCtx.device.allocateDescriptorSets(allocInfo);
    chunk.chunkSet = std::move(sets.front());

    DescriptorWriter writer(vkCtx);
    writer.writeImage(*chunk.chunkSet, 0, vk::DescriptorType::eCombinedImageSampler, *chunk.heightMap->textureImageView,
                      *chunk.heightMap->textureSampler);
    writer.writeImage(*chunk.chunkSet, 1, vk::DescriptorType::eCombinedImageSampler, *chunk.normalMap->textureImageView,
                      *chunk.normalMap->textureSampler);
    writer.update();

    const auto existing = chunks.find(coord);
    if (existing != chunks.end()) {
        retiringChunks.push_back(RetiringChunk{std::move(existing->second), maxFramesInFlight});
        existing->second = std::move(chunk);
    } else {
        chunks.emplace(coord, std::move(chunk));
    }
}

void TerrainPatchRenderer::removeChunk(const TerrainChunkCoord coord) {
    const auto it = chunks.find(coord);
    if (it == chunks.end()) {
        return;
    }

    retiringChunks.push_back(RetiringChunk{std::move(it->second), maxFramesInFlight});
    chunks.erase(it);
}

void TerrainPatchRenderer::tickRetiredChunks() {
    for (auto &retiring : retiringChunks) {
        --retiring.framesRemaining;
    }
    retiringChunks.erase(std::remove_if(retiringChunks.begin(), retiringChunks.end(),
                                        [](const RetiringChunk &r) { return r.framesRemaining <= 0; }),
                         retiringChunks.end());

    for (auto &pending : pendingBakeResources) {
        --pending.framesRemaining;
    }
    pendingBakeResources.erase(std::remove_if(pendingBakeResources.begin(), pendingBakeResources.end(),
                                              [](const PendingBakeResources &p) { return p.framesRemaining <= 0; }),
                               pendingBakeResources.end());
}

void TerrainPatchRenderer::drawChunk(const ChunkGpuData &chunk, const vk::Pipeline pipeline, const DrawCommand &command) const {
    const LodMesh &lodMesh = lodMeshes[chunk.lodIndex];

    TerrainPushConstants pc = pushConstants;
    pc.chunkOrigin = chunk.worldOrigin;
    pc.chunkWorldSize = chunk.worldSize;
    pc.gridDim = lodMesh.gridDim;

    command.commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array<vk::DescriptorSet, 2> sets = {*materialSets[command.frameIndex], *chunk.chunkSet};
    command.commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, sets, nullptr);
    command.commandBuffer->pushConstants<TerrainPushConstants>(
        *pipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, pc);

    const std::vector<vk::DeviceSize> vertexOffsets = {0};
    command.commandBuffer->bindVertexBuffers(0, *lodMesh.mesh->unifiedBuffer, vertexOffsets);
    const vk::DeviceSize vertexSizeOffset = sizeof(Mesh::Vertex) * lodMesh.mesh->vertices.size();
    command.commandBuffer->bindIndexBuffer(*lodMesh.mesh->unifiedBuffer, vertexSizeOffset, vk::IndexType::eUint32);

    command.commandBuffer->drawIndexed(static_cast<uint32_t>(lodMesh.mesh->indices.size()), 1, 0, 0, 0);
}

bool TerrainPatchRenderer::isChunkVisible(const ChunkGpuData &chunk, const CullingUtils::Frustum &frustum) const {
    const glm::vec3 boundsCenter(chunk.worldOrigin.x, chunk.worldOrigin.y, 0.0f);
    const float horizontalRadius = chunk.worldSize * 0.70710678f;
    const float boundsRadius = std::sqrt(horizontalRadius * horizontalRadius + verticalCullExtent * verticalCullExtent);
    return CullingUtils::sphereInFrustum(frustum, boundsCenter, boundsRadius);
}

void TerrainPatchRenderer::draw(const DrawCommand &command, const bool isWireframe, uint32_t &drawCallCount) const {
    lastVisibleIndexCount = 0;
    const vk::Pipeline pipeline = isWireframe ? *wireframePipeline : *solidPipeline;

    for (const auto &[coord, chunk] : chunks) {
        if (!isChunkVisible(chunk, *command.frustum)) {
            continue;
        }
        drawChunk(chunk, pipeline, command);
        drawCallCount++;
        lastVisibleIndexCount += lodMeshes[chunk.lodIndex].mesh->indices.size();
    }
}

void TerrainPatchRenderer::drawShadow(const DrawCommand &command) const {
    for (const auto &[coord, chunk] : chunks) {
        drawChunk(chunk, *shadowPipeline, command);
    }
}

uint64_t TerrainPatchRenderer::getVisibleVertexEstimate() const { return lastVisibleIndexCount; }
