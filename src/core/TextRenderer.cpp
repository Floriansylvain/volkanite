#include "TextRenderer.hpp"
#include "DescriptorWriter.hpp"
#include "PipelineBuilder.hpp"
#include "VulkanUtils.hpp"
#include <cstring>

TextRenderer::TextRenderer(VulkanContext &context, const int maxFramesInFlight)
    : vkCtx(context), maxFramesInFlight(maxFramesInFlight), font(context) {}

void TextRenderer::createDescriptorSetLayout() {
    const vk::DescriptorSetLayoutBinding imageBinding{0, vk::DescriptorType::eSampledImage, 1,
                                                      vk::ShaderStageFlagBits::eFragment};
    const vk::DescriptorSetLayoutBinding samplerBinding{1, vk::DescriptorType::eSampler, 1, vk::ShaderStageFlagBits::eFragment};
    descriptorSetLayout = VulkanUtils::createDescriptorSetLayout(vkCtx, {imageBinding, samplerBinding});
}

void TextRenderer::createDescriptorSet() {
    std::array<vk::DescriptorPoolSize, 2> poolSizes{vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 1},
                                                    vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 1}};

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    descriptorPool = vk::raii::DescriptorPool(vkCtx.device, poolInfo);

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = *descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &*descriptorSetLayout;

    descriptorSet = std::move(vk::raii::DescriptorSets(vkCtx.device, allocInfo).front());

    DescriptorWriter(vkCtx)
        .writeImage(*descriptorSet, 0, vk::DescriptorType::eSampledImage, *font.texture.textureImageView)
        .writeImage(*descriptorSet, 1, vk::DescriptorType::eSampler, nullptr, *font.texture.textureSampler)
        .update();
}

void TextRenderer::createVertexBuffers() {
    const vk::DeviceSize bufferSize = MAX_CHARS * VERTICES_PER_CHAR * sizeof(Font::TextVertex);
    vertexBuffers =
        PerFrameBuffer(vkCtx, static_cast<uint32_t>(maxFramesInFlight), bufferSize, vk::BufferUsageFlagBits::eVertexBuffer,
                       vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
}

void TextRenderer::loadFont(const std::string &path, const vk::raii::CommandPool &commandPool, const float spacing,
                            const float wordSpacingFactor) {
    letterSpacing = spacing;
    wordSpacing = wordSpacingFactor;
    font.load(path, commandPool);
    createDescriptorSet();
    createVertexBuffers();
}

void TextRenderer::createPipeline(const vk::Format colorFormat, const vk::Format depthFormat) {
    pipelineLayout = VulkanUtils::createPipelineLayout(vkCtx, {*descriptorSetLayout});

    const auto bindingDescription = Font::TextVertex::getBindingDescription();
    const auto attributeDescriptions = Font::TextVertex::getAttributeDescriptions();
    const std::vector attrs(attributeDescriptions.begin(), attributeDescriptions.end());

    pipeline = GraphicsPipelineBuilder(vkCtx)
                   .addShaderStage(vk::ShaderStageFlagBits::eVertex, "shaders/text.spv", "vertMainText")
                   .addShaderStage(vk::ShaderStageFlagBits::eFragment, "shaders/text.spv", "fragMainText")
                   .setVertexInput({bindingDescription}, attrs)
                   .setCullMode(vk::CullModeFlagBits::eNone)
                   .setDepthTest(false, false)
                   .setLayout(*pipelineLayout)
                   .setColorFormats({colorFormat})
                   .setDepthFormat(depthFormat)
                   .setMSAA(vkCtx.msaaSamples)
                   .build();
}

void TextRenderer::drawText(const std::string &text, const float pixelX, const float pixelY, const float pixelHeight,
                            const glm::vec3 &color, const vk::Extent2D screenExtent) {
    constexpr float REFERENCE_HEIGHT = 1080.0f;
    const float scale = static_cast<float>(screenExtent.height) / REFERENCE_HEIGHT;

    const float scaledHeight = pixelHeight * scale;
    float cursorX = pixelX * scale;
    const float scaledY = pixelY * scale;

    for (const char c : text) {
        if (pendingVertices.size() + VERTICES_PER_CHAR > MAX_CHARS * VERTICES_PER_CHAR) {
            break;
        }
        if (c == ' ') {
            cursorX += scaledHeight * wordSpacing;
            continue;
        }

        const auto [u0, v0, u1, v1] = Font::glyphUV(c);
        auto toNdc = [&](const float px, const float py) {
            return glm::vec2((px / static_cast<float>(screenExtent.width)) * 2.0f - 1.0f,
                             (py / static_cast<float>(screenExtent.height)) * 2.0f - 1.0f);
        };

        const glm::vec2 topLeft = toNdc(cursorX, scaledY);
        const glm::vec2 topRight = toNdc(cursorX + scaledHeight, scaledY);
        const glm::vec2 bottomLeft = toNdc(cursorX, scaledY + scaledHeight);
        const glm::vec2 bottomRight = toNdc(cursorX + scaledHeight, scaledY + scaledHeight);

        pendingVertices.push_back({topLeft, color, {u0, v0}});
        pendingVertices.push_back({topRight, color, {u1, v0}});
        pendingVertices.push_back({bottomRight, color, {u1, v1}});
        pendingVertices.push_back({bottomRight, color, {u1, v1}});
        pendingVertices.push_back({bottomLeft, color, {u0, v1}});
        pendingVertices.push_back({topLeft, color, {u0, v0}});

        cursorX += scaledHeight * letterSpacing;
    }
}

void TextRenderer::render(const vk::raii::CommandBuffer &commandBuffer, const uint32_t frameIndex) {
    if (pendingVertices.empty()) {
        return;
    }

    const vk::DeviceSize copySize = pendingVertices.size() * sizeof(Font::TextVertex);
    std::memcpy(vertexBuffers.mapped(frameIndex), pendingVertices.data(), copySize);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, *descriptorSet, nullptr);

    const std::vector<vk::DeviceSize> offsets = {0};
    commandBuffer.bindVertexBuffers(0, vertexBuffers[frameIndex], offsets);
    commandBuffer.draw(static_cast<uint32_t>(pendingVertices.size()), 1, 0, 0);

    pendingVertices.clear();
}
