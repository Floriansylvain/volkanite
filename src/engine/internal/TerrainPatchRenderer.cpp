#include "TerrainPatchRenderer.hpp"
#include "PipelineBuilder.hpp"
#include "VulkanUtils.hpp"
#include <algorithm>

TerrainPatchRenderer::TerrainPatchRenderer(VulkanContext &vkCtx, const int maxFramesInFlight)
    : vkCtx(vkCtx), maxFramesInFlight(maxFramesInFlight) {}

void TerrainPatchRenderer::createGridMesh(const vk::raii::CommandPool &commandPool, const TerrainConfig &config) {
    using enum vk::BufferUsageFlagBits;
    using enum vk::MemoryPropertyFlagBits;

    instanceBuffers = PerFrameBuffer(vkCtx, static_cast<uint32_t>(maxFramesInFlight),
                                     sizeof(TerrainPatchInstance) * MAX_PATCHES, eVertexBuffer, eHostVisible | eHostCoherent);

    const int resolution = std::max(config.chunkResolution, 2);

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

    gridMesh = std::make_shared<Mesh>(vkCtx);
    gridMesh->vertices = std::move(vertices);
    gridMesh->indices = std::move(indices);
    gridMesh->createGeometryBuffers(commandPool);

    pushConstants.gridDim = static_cast<float>(resolution - 1);
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
}

void TerrainPatchRenderer::createPipelines(const vk::DescriptorSetLayout materialSetLayout, const vk::Format colorFormat,
                                           const vk::Format depthFormat, const vk::Format shadowDepthFormat) {
    vk::PushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
    pushConstantRange.size = sizeof(TerrainPushConstants);

    pipelineLayout = VulkanUtils::createPipelineLayout(vkCtx, {materialSetLayout}, {pushConstantRange});

    const auto attributeDescriptions = Mesh::Vertex::getAttributeDescriptions();
    const vk::VertexInputBindingDescription bindingDescription = Mesh::Vertex::getBindingDescription();

    const vk::VertexInputBindingDescription instanceBinding{1, sizeof(TerrainPatchInstance), vk::VertexInputRate::eInstance};
    const vk::VertexInputAttributeDescription instanceOriginAttr{5, 1, vk::Format::eR32G32Sfloat,
                                                                 offsetof(TerrainPatchInstance, origin)};
    const vk::VertexInputAttributeDescription instanceParamsAttr{6, 1, vk::Format::eR32G32B32A32Sfloat,
                                                                 offsetof(TerrainPatchInstance, params)};

    const std::vector<vk::VertexInputAttributeDescription> patchAttrs = {attributeDescriptions[0], instanceOriginAttr,
                                                                         instanceParamsAttr};

    GraphicsPipelineBuilder builder(vkCtx);
    builder.addShaderStage(vk::ShaderStageFlagBits::eVertex, "shaders/shader.spv", "vertTerrain")
        .addShaderStage(vk::ShaderStageFlagBits::eFragment, "shaders/shader.spv", "fragMain")
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
        .addShaderStage(vk::ShaderStageFlagBits::eFragment, "shaders/shader.spv", "fragShadow")
        .setVertexInput({bindingDescription, instanceBinding}, patchAttrs)
        .setLayout(*pipelineLayout)
        .setColorFormats({})
        .setDepthFormat(shadowDepthFormat)
        .setDepthBias(1.5f, 1.75f);

    shadowPipeline = shadowBuilder.build();
}

void TerrainPatchRenderer::setPatches(std::vector<TerrainPatchInstance> patches) { pendingPatches = std::move(patches); }

void TerrainPatchRenderer::upload(const uint32_t frameIndex) {
    activeInstanceCount = static_cast<uint32_t>(std::min(pendingPatches.size(), MAX_PATCHES));
    if (activeInstanceCount == 0) {
        return;
    }
    std::memcpy(instanceBuffers.mapped(frameIndex), pendingPatches.data(), sizeof(TerrainPatchInstance) * activeInstanceCount);
}

void TerrainPatchRenderer::draw(const DrawCommand &command, bool isWireframe, uint32_t &drawCallCount) const {
    if (activeInstanceCount == 0) {
        return;
    }
    command.commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, isWireframe ? *wireframePipeline : *solidPipeline);
    command.commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, command.materialSet,
                                              nullptr);
    command.commandBuffer->pushConstants<TerrainPushConstants>(*pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0,
                                                               pushConstants);

    const std::vector<vk::DeviceSize> vertexOffsets = {0};
    command.commandBuffer->bindVertexBuffers(0, *gridMesh->unifiedBuffer, vertexOffsets);
    const std::vector<vk::DeviceSize> instanceOffsets = {0};
    command.commandBuffer->bindVertexBuffers(1, instanceBuffers[command.frameIndex], instanceOffsets);
    const vk::DeviceSize vertexSizeOffset = sizeof(Mesh::Vertex) * gridMesh->vertices.size();
    command.commandBuffer->bindIndexBuffer(*gridMesh->unifiedBuffer, vertexSizeOffset, vk::IndexType::eUint32);

    command.commandBuffer->drawIndexed(static_cast<uint32_t>(gridMesh->indices.size()), activeInstanceCount, 0, 0, 0);
    drawCallCount++;
}

void TerrainPatchRenderer::drawShadow(const DrawCommand &command) const {
    if (activeInstanceCount == 0) {
        return;
    }

    command.commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *shadowPipeline);
    command.commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, command.materialSet,
                                              nullptr);
    command.commandBuffer->pushConstants<TerrainPushConstants>(*pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0,
                                                               pushConstants);

    const std::vector<vk::DeviceSize> vertexOffsets = {0};
    command.commandBuffer->bindVertexBuffers(0, *gridMesh->unifiedBuffer, vertexOffsets);
    const std::vector<vk::DeviceSize> instanceOffsets = {0};
    command.commandBuffer->bindVertexBuffers(1, instanceBuffers[command.frameIndex], instanceOffsets);
    const vk::DeviceSize vertexSizeOffset = sizeof(Mesh::Vertex) * gridMesh->vertices.size();
    command.commandBuffer->bindIndexBuffer(*gridMesh->unifiedBuffer, vertexSizeOffset, vk::IndexType::eUint32);

    command.commandBuffer->drawIndexed(static_cast<uint32_t>(gridMesh->indices.size()), activeInstanceCount, 0, 0, 0);
}
