#ifndef PIPELINE_HPP
#define PIPELINE_HPP

#include "Device.hpp"
#include "SwapChain.hpp"
#include <glm/glm.hpp>

// GPU Program
class Pipeline {
  public:
    Pipeline(Device &device, SwapChain &swapChain);
    ~Pipeline();

    Pipeline(const Pipeline &) = delete;
    Pipeline &operator=(const Pipeline &) = delete;

    vk::raii::PipelineLayout *getPipelineLayout();
    vk::raii::Pipeline *getGraphicsPipeline();
    vk::raii::Buffer *getVertexBuffer();

  private:
    Device &device;
    SwapChain &swapChain;

    std::optional<vk::raii::PipelineLayout> pipelineLayout;
    std::optional<vk::raii::Pipeline> graphicsPipeline;
    std::optional<vk::raii::Buffer> vertexBuffer;
    std::optional<vk::raii::DeviceMemory> vertexBufferMemory;
    std::optional<vk::MemoryRequirements> memRequirements;

    void initPipelineLayout();
    void initGraphicsPipeline();
    void initVertexBuffer();

    std::vector<char> readFile(const std::string &filename);
    vk::raii::ShaderModule createShaderModule(const std::vector<char> &code);
    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);
};

#endif
