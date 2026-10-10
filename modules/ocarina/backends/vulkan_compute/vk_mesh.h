//
// Created during CUDA→Slang+Vulkan migration.
// Replaces CUDAMesh: wraps a Vulkan BLAS (bottom-level acceleration structure).
//

#pragma once

#include "core/stl.h"
#include "util.h"
#include "rhi/rtx/mesh.h"
#include <vulkan/vulkan.h>

namespace ocarina {

class VulkanComputeDevice;

class VkMesh final : public RHIMesh::Impl {
private:
    VulkanComputeDevice *device_;
    MeshParams           params_;

    VkAccelerationStructureKHR  blas_{VK_NULL_HANDLE};
    VkBufferAllocation          blas_buf_{};
    VkDeviceAddress             blas_address_{0};
    VkBufferAllocation          packed_indices_{};

    VkAccelerationStructureGeometryKHR            geom_{};
    VkAccelerationStructureBuildGeometryInfoKHR   build_info_{};

public:
    VkMesh(VulkanComputeDevice *device, const MeshParams &params)
        : device_(device), params_(params) {}
    ~VkMesh() noexcept override;

    void init_build_input() noexcept;
    void clear() noexcept;
    void build_bvh(const BLASBuildCommand *cmd) noexcept;

    [[nodiscard]] handle_ty blas_handle() const noexcept override { return blas_address_; }
    [[nodiscard]] uint vertex_num()       const noexcept override { return params_.vert_num; }
    [[nodiscard]] uint triangle_num()     const noexcept override { return params_.tri_num; }
};

}// namespace ocarina
