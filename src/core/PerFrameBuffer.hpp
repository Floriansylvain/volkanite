#ifndef PER_FRAME_BUFFER_HPP
#define PER_FRAME_BUFFER_HPP

class PerFrameBuffer {
  public:
    PerFrameBuffer() = default;
    PerFrameBuffer(const VulkanContext &vkCtx, uint32_t frameCount, vk::DeviceSize size, vk::BufferUsageFlags usage,
                   vk::MemoryPropertyFlags properties);

    [[nodiscard]] vk::Buffer operator[](size_t frame) const { return *buffers[frame]; }
    [[nodiscard]] void *mapped(size_t frame) const { return mappedPtrs[frame]; }
    [[nodiscard]] std::vector<vk::Buffer> handles() const;

  private:
    std::vector<vk::raii::Buffer> buffers;
    std::vector<vk::raii::DeviceMemory> memories;
    std::vector<void *> mappedPtrs;
};

#endif
