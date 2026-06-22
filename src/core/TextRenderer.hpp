#ifndef TEXT_RENDERER_HPP
#define TEXT_RENDERER_HPP

#pragma once
#include "Font.hpp"

class TextRenderer {
  public:
    explicit TextRenderer(VulkanContext &context, int maxFramesInFlight);

    void loadFont(const std::string &path, const vk::raii::CommandPool &commandPool, float letterSpacing = 1.0f,
                  float wordSpacing = 1.0f);

    void createDescriptorSetLayout();
    void createPipeline(const vk::PipelineShaderStageCreateInfo &vertStage, const vk::PipelineShaderStageCreateInfo &fragStage,
                        const vk::PipelineRenderingCreateInfo &pipelineRenderingCreateInfo,
                        vk::SampleCountFlagBits msaaSamples);

    void drawText(const std::string &text, float pixelX, float pixelY, float pixelHeight, const glm::vec3 &color,
                  vk::Extent2D screenExtent);

    void render(const vk::raii::CommandBuffer &commandBuffer, uint32_t frameIndex);

  private:
    static constexpr size_t MAX_CHARS = 2048;
    static constexpr size_t VERTICES_PER_CHAR = 6;

    VulkanContext &vkCtx;
    int maxFramesInFlight;

    Font font;
    float letterSpacing = 1.0f;
    float wordSpacing = 1.0f;

    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::DescriptorPool descriptorPool = nullptr;
    vk::raii::DescriptorSet descriptorSet = nullptr;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;

    std::vector<vk::raii::Buffer> vertexBuffers;
    std::vector<vk::raii::DeviceMemory> vertexBuffersMemory;
    std::vector<void *> vertexBuffersMapped;
    std::vector<Font::TextVertex> pendingVertices;

    void createDescriptorSet();
    void createVertexBuffers();
};

#endif
