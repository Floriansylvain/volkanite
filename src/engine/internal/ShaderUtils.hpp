#ifndef SHADER_UTILS_HPP
#define SHADER_UTILS_HPP

namespace ShaderUtils {

std::vector<char> readFile(const std::string &filename);

[[nodiscard]] vk::raii::ShaderModule createShaderModule(const VulkanContext &vkCtx, const std::vector<char> &code);

} // namespace ShaderUtils

#endif
