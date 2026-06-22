#include "ShaderUtils.hpp"
#include "Exceptions.hpp"
#include <fstream>

std::vector<char> ShaderUtils::readFile(const std::string &filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw EngineExceptions::Shader("Failed to open file");
    }

    std::vector<char> buffer(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    file.close();

    return buffer;
}

vk::raii::ShaderModule ShaderUtils::createShaderModule(const VulkanContext &vkCtx, const std::vector<char> &code) {
    if (code.size() % sizeof(uint32_t) != 0) {
        throw EngineExceptions::Shader("SPIR-V code size must be a multiple of 4 bytes.");
    }

    std::vector<uint32_t> alignedCode(code.size() / sizeof(uint32_t));
    std::memcpy(alignedCode.data(), code.data(), code.size());

    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = code.size();
    createInfo.pCode = alignedCode.data();

    vk::raii::ShaderModule shaderModule{vkCtx.device, createInfo};

    return shaderModule;
}
