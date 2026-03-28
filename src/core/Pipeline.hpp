#ifndef PIPELINE_HPP
#define PIPELINE_HPP

#include "Device.hpp"
#include "SwapChain.hpp"

// GPU Program
class Pipeline {
  public:
    Pipeline(Device &device, SwapChain &swapChain);
    ~Pipeline();

    Pipeline(const Pipeline &) = delete;
    Pipeline &operator=(const Pipeline &) = delete;

    vk::raii::PipelineLayout *getPipelineLayout();
    vk::raii::Pipeline *getGraphicsPipeline();

  private:
    Device &device;
    SwapChain &swapChain;

    std::optional<vk::raii::PipelineLayout> pipelineLayout;
    std::optional<vk::raii::Pipeline> graphicsPipeline;

    void initPipelineLayout();
    void initGraphicsPipeline();

    std::vector<char> readFile(const std::string &filename);
    vk::raii::ShaderModule createShaderModule(const std::vector<char> &code);
};

#endif
