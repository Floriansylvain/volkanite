#ifndef SHADER_UTILS_HPP
#define SHADER_UTILS_HPP

#pragma once
#include "VulkanContext.hpp"
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace ShaderUtils {

std::vector<char> readFile(const std::string &filename);

[[nodiscard]] vk::raii::ShaderModule createShaderModule(const VulkanContext &vkCtx, const std::vector<char> &code);

} // namespace ShaderUtils

#endif
