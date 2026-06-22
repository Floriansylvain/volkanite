#ifndef DESCRIPTOR_WRITER_HPP
#define DESCRIPTOR_WRITER_HPP

#pragma once
#include <deque>

class DescriptorWriter {
  public:
    explicit DescriptorWriter(const VulkanContext &vkCtx);

    DescriptorWriter &writeBuffer(vk::DescriptorSet set, uint32_t binding, vk::Buffer buffer, vk::DeviceSize range,
                                  vk::DescriptorType type, vk::DeviceSize offset = 0);

    DescriptorWriter &writeImage(vk::DescriptorSet set, uint32_t binding, vk::DescriptorType type,
                                 vk::ImageView imageView = nullptr, vk::Sampler sampler = nullptr,
                                 vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);

    void update();

  private:
    const VulkanContext &vkCtx;

    std::deque<vk::DescriptorBufferInfo> bufferInfos;
    std::deque<vk::DescriptorImageInfo> imageInfos;
    std::vector<vk::WriteDescriptorSet> writes;
};

#endif
