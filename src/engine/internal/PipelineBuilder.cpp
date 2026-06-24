#include "PipelineBuilder.hpp"
#include "Exceptions.hpp"
#include "ShaderUtils.hpp"

GraphicsPipelineBuilder::GraphicsPipelineBuilder(const VulkanContext &vkCtx) : vkCtx(vkCtx) {}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::addShaderStage(const vk::ShaderStageFlagBits stage, const std::string &path,
                                                                 const char *entryPoint) {
    ownedModules.push_back(ShaderUtils::createShaderModule(vkCtx, ShaderUtils::readFile(path)));

    vk::PipelineShaderStageCreateInfo info{};
    info.stage = stage;
    info.module = *ownedModules.back();
    info.pName = entryPoint;
    stages.push_back(info);
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::overrideEntryPoint(const vk::ShaderStageFlagBits stage,
                                                                     const char *entryPoint) {
    for (auto &s : stages) {
        if (s.stage == stage) {
            s.pName = entryPoint;
            return *this;
        }
    }
    throw EngineExceptions::Shader("overrideEntryPoint: no stage matching that flag was added");
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setVertexInput(std::vector<vk::VertexInputBindingDescription> _bindings,
                                                                 std::vector<vk::VertexInputAttributeDescription> _attributes) {
    bindings = std::move(_bindings);
    attributes = std::move(_attributes);
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setTopology(const vk::PrimitiveTopology _topology) {
    topology = _topology;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setPolygonMode(const vk::PolygonMode mode) {
    polygonMode = mode;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setCullMode(const vk::CullModeFlags mode, const vk::FrontFace face) {
    cullMode = mode;
    frontFace = face;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setDepthTest(const bool testEnable, const bool writeEnable,
                                                               const vk::CompareOp op) {
    depthTestEnable = testEnable;
    depthWriteEnable = writeEnable;
    depthCompareOp = op;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setDepthBias(const float constantFactor, const float slopeFactor,
                                                               const float clamp) {
    depthBiasEnable = true;
    depthBiasConstantFactor = constantFactor;
    depthBiasSlopeFactor = slopeFactor;
    depthBiasClamp = clamp;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setBlendEnabled(const bool enabled) {
    blendEnable = enabled;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setColorFormats(std::vector<vk::Format> formats) {
    colorFormats = std::move(formats);
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setDepthFormat(const vk::Format format) {
    depthFormat = format;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setMSAA(const vk::SampleCountFlagBits samples) {
    msaaSamples = samples;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setLayout(const vk::PipelineLayout _layout) {
    layout = _layout;
    return *this;
}

vk::raii::Pipeline GraphicsPipelineBuilder::build() {
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInputInfo.pVertexBindingDescriptions = bindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = topology;

    const std::vector dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.polygonMode = polygonMode;
    rasterizer.cullMode = cullMode;
    rasterizer.frontFace = frontFace;
    rasterizer.lineWidth = 1.0f;
    rasterizer.depthBiasEnable = depthBiasEnable;
    rasterizer.depthBiasConstantFactor = depthBiasConstantFactor;
    rasterizer.depthBiasSlopeFactor = depthBiasSlopeFactor;
    rasterizer.depthBiasClamp = depthBiasClamp;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.rasterizationSamples = msaaSamples;

    using enum vk::ColorComponentFlagBits;
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.blendEnable = blendEnable;
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
    depthStencil.depthTestEnable = depthTestEnable;
    depthStencil.depthWriteEnable = depthWriteEnable;
    depthStencil.depthCompareOp = depthCompareOp;

    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
    renderingInfo.pColorAttachmentFormats = colorFormats.data();
    renderingInfo.depthAttachmentFormat = depthFormat;

    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = layout;
    pipelineInfo.renderPass = nullptr;

    vk::StructureChain chain = {pipelineInfo, renderingInfo};
    return {vkCtx.device, nullptr, chain.get<vk::GraphicsPipelineCreateInfo>()};
}

ComputePipelineBuilder::ComputePipelineBuilder(const VulkanContext &vkCtx) : vkCtx(vkCtx) {}

vk::raii::Pipeline ComputePipelineBuilder::build(const std::string &path, const vk::PipelineLayout layout,
                                                 const char *entryPoint) const {
    const vk::raii::ShaderModule shaderModule = ShaderUtils::createShaderModule(vkCtx, ShaderUtils::readFile(path));

    vk::PipelineShaderStageCreateInfo stageInfo{};
    stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    stageInfo.module = *shaderModule;
    stageInfo.pName = entryPoint;

    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = layout;

    return {vkCtx.device, nullptr, pipelineInfo};
}
