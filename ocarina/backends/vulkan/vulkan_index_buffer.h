#pragma once
#include "core/header.h"
#include "core/concepts.h"
#include "core/stl.h"
#include "core/concurrency/singleton.h"
#include "rhi/index_buffer.h"
#include <vulkan/vulkan.h>
#include "vulkan_buffer.h"
namespace ocarina {
class VulkanDevice;

class VulkanIndexBuffer : public IndexBuffer {
public:
    VulkanIndexBuffer(VulkanDevice *device, const void* initial_data, uint32_t indices_count, bool bit16);
    ~VulkanIndexBuffer();
    void load_from_cpu(const void *cpu_data, uint32_t byte_offset,
                     uint32_t num_bytes);

    VkBuffer buffer_handle() const
    {
        return vulkan_buffer_ != nullptr ? vulkan_buffer_->buffer_handle() : VK_NULL_HANDLE;
    }
private:
    VulkanDevice *device_ = nullptr;
    VulkanBuffer* vulkan_buffer_ = nullptr;

};

}// namespace ocarina