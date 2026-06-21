#include "Font.hpp"

vk::VertexInputBindingDescription Font::TextVertex::getBindingDescription() {
    vk::VertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Font::TextVertex);
    bindingDescription.inputRate = vk::VertexInputRate::eVertex;
    return bindingDescription;
}

std::array<vk::VertexInputAttributeDescription, 3> Font::TextVertex::getAttributeDescriptions() {
    vk::VertexInputAttributeDescription posDescription{};
    posDescription.location = 0;
    posDescription.binding = 0;
    posDescription.format = vk::Format::eR32G32Sfloat;
    posDescription.offset = offsetof(Font::TextVertex, pos);

    vk::VertexInputAttributeDescription colorDescription{};
    colorDescription.location = 1;
    colorDescription.binding = 0;
    colorDescription.format = vk::Format::eR32G32B32Sfloat;
    colorDescription.offset = offsetof(Font::TextVertex, color);

    vk::VertexInputAttributeDescription texCoordDescription{};
    texCoordDescription.location = 2;
    texCoordDescription.binding = 0;
    texCoordDescription.format = vk::Format::eR32G32Sfloat;
    texCoordDescription.offset = offsetof(Font::TextVertex, texCoord);

    return {posDescription, colorDescription, texCoordDescription};
}
