#include "TextRenderer.hpp"
#include "VulkanUtils.hpp"
#include <cstring>

TextRenderer::TextRenderer(VulkanContext &context, const int maxFramesInFlight)
    : vkCtx(context), maxFramesInFlight(maxFramesInFlight), font(context) {}

void TextRenderer::createDescriptorSetLayout() {
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = vk::DescriptorType::eSampledImage;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

    bindings[1].binding = 1;
    bindings[1].descriptorType = vk::DescriptorType::eSampler;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    descriptorSetLayout = vk::raii::DescriptorSetLayout(vkCtx.device, layoutInfo);
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

    vk::DescriptorImageInfo textureInfo{};
    textureInfo.imageView = *font.texture.textureImageView;
    textureInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::DescriptorImageInfo samplerInfo{};
    samplerInfo.sampler = *font.texture.textureSampler;

    std::array<vk::WriteDescriptorSet, 2> descriptorWrites{};

    descriptorWrites[0].dstSet = *descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].descriptorType = vk::DescriptorType::eSampledImage;
    descriptorWrites[0].pImageInfo = &textureInfo;

    descriptorWrites[1].dstSet = *descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].descriptorType = vk::DescriptorType::eSampler;
    descriptorWrites[1].pImageInfo = &samplerInfo;

    vkCtx.device.updateDescriptorSets(descriptorWrites, {});
}

void TextRenderer::createVertexBuffers() {
    vk::DeviceSize bufferSize = MAX_CHARS * VERTICES_PER_CHAR * sizeof(Font::TextVertex);

    for (int i = 0; i < maxFramesInFlight; i++) {
        auto [buffer, memory] =
            VulkanUtils::createBuffer(vkCtx, bufferSize, vk::BufferUsageFlagBits::eVertexBuffer,
                                      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

        vertexBuffers.push_back(std::move(buffer));
        vertexBuffersMemory.push_back(std::move(memory));
        vertexBuffersMapped.push_back(vertexBuffersMemory.back().mapMemory(0, bufferSize));
    }
}

void TextRenderer::loadFont(const std::string &path, const vk::raii::CommandPool &commandPool, const float spacing,
                            const float wordSpacingFactor) {
    letterSpacing = spacing;
    wordSpacing = wordSpacingFactor;
    font.load(path, commandPool);
    createDescriptorSet();
    createVertexBuffers();
}

void TextRenderer::createPipeline(const vk::PipelineShaderStageCreateInfo &vertStage,
                                  const vk::PipelineShaderStageCreateInfo &fragStage,
                                  const vk::PipelineRenderingCreateInfo &pipelineRenderingCreateInfo,
                                  const vk::SampleCountFlagBits msaaSamples) {
    const std::array shaderStages = {vertStage, fragStage};

    auto bindingDescription = Font::TextVertex::getBindingDescription();
    auto attributeDescriptions = Font::TextVertex::getAttributeDescriptions();

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

    const std::vector dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.cullMode = vk::CullModeFlagBits::eNone;
    rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
    rasterizer.lineWidth = 1.0f;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.rasterizationSamples = msaaSamples;

    using enum vk::ColorComponentFlagBits;
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = vk::True;
    colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachment.colorWriteMask = eR | eG | eB | eA;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable = vk::False;
    depthStencil.depthWriteEnable = vk::False;

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &*descriptorSetLayout;

    pipelineLayout = vk::raii::PipelineLayout(vkCtx.device, pipelineLayoutInfo);

    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = nullptr;

    vk::StructureChain chain = {pipelineInfo, pipelineRenderingCreateInfo};
    pipeline = vk::raii::Pipeline(vkCtx.device, nullptr, chain.get<vk::GraphicsPipelineCreateInfo>());
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
    std::memcpy(vertexBuffersMapped[frameIndex], pendingVertices.data(), copySize);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, *descriptorSet, nullptr);

    const std::vector<vk::DeviceSize> offsets = {0};
    commandBuffer.bindVertexBuffers(0, *vertexBuffers[frameIndex], offsets);
    commandBuffer.draw(static_cast<uint32_t>(pendingVertices.size()), 1, 0, 0);

    pendingVertices.clear();
}
