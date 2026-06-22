#ifndef PIPELINE_BUILDER_HPP
#define PIPELINE_BUILDER_HPP

class GraphicsPipelineBuilder {
  public:
    explicit GraphicsPipelineBuilder(const VulkanContext &vkCtx);

    GraphicsPipelineBuilder &addShaderStage(vk::ShaderStageFlagBits stage, const std::string &path,
                                            const char *entryPoint = "main");
    GraphicsPipelineBuilder &overrideEntryPoint(vk::ShaderStageFlagBits stage, const char *entryPoint);

    GraphicsPipelineBuilder &setVertexInput(std::vector<vk::VertexInputBindingDescription> bindings,
                                            std::vector<vk::VertexInputAttributeDescription> attributes);

    GraphicsPipelineBuilder &setTopology(vk::PrimitiveTopology topology);
    GraphicsPipelineBuilder &setPolygonMode(vk::PolygonMode mode);
    GraphicsPipelineBuilder &setCullMode(vk::CullModeFlags mode, vk::FrontFace face = vk::FrontFace::eCounterClockwise);
    GraphicsPipelineBuilder &setDepthTest(bool testEnable, bool writeEnable = true, vk::CompareOp op = vk::CompareOp::eLess);
    GraphicsPipelineBuilder &setBlendEnabled(bool enabled);
    GraphicsPipelineBuilder &setColorFormats(std::vector<vk::Format> formats);
    GraphicsPipelineBuilder &setDepthFormat(vk::Format format);
    GraphicsPipelineBuilder &setMSAA(vk::SampleCountFlagBits samples);
    GraphicsPipelineBuilder &setLayout(vk::PipelineLayout layout);

    [[nodiscard]] vk::raii::Pipeline build();

  private:
    const VulkanContext &vkCtx;

    std::vector<vk::raii::ShaderModule> ownedModules;
    std::vector<vk::PipelineShaderStageCreateInfo> stages;

    std::vector<vk::VertexInputBindingDescription> bindings;
    std::vector<vk::VertexInputAttributeDescription> attributes;

    vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
    vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
    vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;
    vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    vk::CompareOp depthCompareOp = vk::CompareOp::eLess;
    bool blendEnable = true;
    std::vector<vk::Format> colorFormats;
    vk::Format depthFormat = vk::Format::eUndefined;
    vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;
    vk::PipelineLayout layout;
};

class ComputePipelineBuilder {
  public:
    explicit ComputePipelineBuilder(const VulkanContext &vkCtx);

    [[nodiscard]] vk::raii::Pipeline build(const std::string &path, vk::PipelineLayout layout,
                                           const char *entryPoint = "main") const;

  private:
    const VulkanContext &vkCtx;
};

#endif
