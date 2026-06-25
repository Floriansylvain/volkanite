#include "DescriptorWriter.hpp"

DescriptorWriter::DescriptorWriter(const VulkanContext &vkCtx) : vkCtx(vkCtx) {}

DescriptorWriter &DescriptorWriter::writeBuffer(const vk::DescriptorSet set, const uint32_t binding, const vk::Buffer buffer,
                                                const vk::DeviceSize range, const vk::DescriptorType type,
                                                const vk::DeviceSize offset) {
    const vk::DescriptorBufferInfo &info = bufferInfos.emplace_back(buffer, offset, range);

    vk::WriteDescriptorSet write{};
    write.dstSet = set;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pBufferInfo = &info;
    writes.push_back(write);
    return *this;
}

DescriptorWriter &DescriptorWriter::writeImage(const vk::DescriptorSet set, const uint32_t binding,
                                               const vk::DescriptorType type, const vk::ImageView imageView,
                                               const vk::Sampler sampler, const vk::ImageLayout layout,
                                               const uint32_t arrayElement) {
    vk::DescriptorImageInfo &info = imageInfos.emplace_back();
    info.sampler = sampler;
    info.imageView = imageView;
    info.imageLayout = layout;

    vk::WriteDescriptorSet write{};
    write.dstSet = set;
    write.dstBinding = binding;
    write.dstArrayElement = arrayElement;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pImageInfo = &info;
    writes.push_back(write);
    return *this;
}

void DescriptorWriter::update() {
    if (writes.empty())
        return;

    vkCtx.device.updateDescriptorSets(writes, {});

    writes.clear();
    bufferInfos.clear();
    imageInfos.clear();
}
