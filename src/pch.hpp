#ifndef PCH_HPP
#define PCH_HPP

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "VulkanContext.hpp"
#include <chrono>
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#endif // PCH_HPP
