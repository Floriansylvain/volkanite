#ifndef FONT_HPP
#define FONT_HPP

#pragma once
#include "Texture.hpp"
#include "VulkanContext.hpp"
#include <array>
#include <glm/glm.hpp>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

class Font {
  public:
    struct TextVertex {
        glm::vec2 pos;
        glm::vec3 color;
        glm::vec2 texCoord;

        static vk::VertexInputBindingDescription getBindingDescription();
        static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions();
    };

    static constexpr int COLS = 16;
    static constexpr int ROWS = 16;
    static constexpr int FIRST_CHAR = 33;

    explicit Font(const VulkanContext &context) : texture(context) {}

    void load(const std::string &path, const vk::raii::CommandPool &commandPool) { texture.loadFromFile(path, commandPool); }

    [[nodiscard]] std::array<float, 4> glyphUV(char c) const {
        int index = static_cast<unsigned char>(c) - FIRST_CHAR;
        if (index < 0 || index >= COLS * ROWS) {
            index = 0;
        }
        const int col = index % COLS;
        const int row = index / COLS;
        const float u0 = static_cast<float>(col) / COLS;
        const float v0 = static_cast<float>(row) / ROWS;
        return {u0, v0, u0 + 1.0f / COLS, v0 + 1.0f / ROWS};
    }

    Texture texture;
};

#endif
