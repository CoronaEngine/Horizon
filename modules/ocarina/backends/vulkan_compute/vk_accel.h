//
// Created during CUDA→Slang+Vulkan migration.
// Replaces OptixAccel: Vulkan TLAS (top-level acceleration structure).
//

#pragma once

#include "core/stl.h"
#include "rhi/rtx/accel.h"
#include <vulkan/vulkan.h>

namespace ocarina {

class VulkanComputeDevice;
class VkComputeCommandVisitor;

class VkAccel final : public Accel::Impl {
private:
    VulkanComputeDevice *device_;

    VkAccelerationStructureKHR tlas_{VK_NULL_HANDLE};
    VkBufferAllocation         tlas_buf_{};
    VkDeviceAddress            tlas_address_{0};

    VkBufferAllocation instances_buf_{};  // VkAccelerationStructureInstanceKHR array

    VkAccelerationStructureBuildGeometryInfoKHR   build_info_{};
    VkAccelerationStructureGeometryKHR            tlas_geom_{};

    bool built_{false};

    [[nodiscard]] VkBuildAccelerationStructureFlagsKHR vk_build_flags() const noexcept;
    void reallocate_tlas(uint32_t instance_count) noexcept;

public:
    explicit VkAccel(VulkanComputeDevice *device, AccelUsageTag usage_tag)
        : Accel::Impl(usage_tag), device_(device) {}
    ~VkAccel() noexcept;

    void build_tlas(VkComputeCommandVisitor *visitor) noexcept;
    void update_tlas(VkComputeCommandVisitor *visitor) noexcept;

    [[nodiscard]] handle_ty      handle()      const noexcept override { return tlas_address_; }
    [[nodiscard]] const void    *handle_ptr()  const noexcept override { return &tlas_address_; }
    [[nodiscard]] size_t         data_size()   const noexcept override { return sizeof(VkDeviceAddress); }
    [[nodiscard]] size_t         data_alignment() const noexcept override { return alignof(VkDeviceAddress); }
    void                         clear()       noexcept override;
};

}// namespace ocarina
