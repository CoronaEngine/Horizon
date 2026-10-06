//
// Created during CUDA→Slang+Vulkan migration.
// Replaces cuda_mesh.cpp — Vulkan BLAS build.
//

#include "vk_mesh.h"
#include "vk_compute_device.h"
#include "rhi/command.h"
#include "util.h"

namespace ocarina {

VkMesh::~VkMesh() noexcept {
    if (blas_ != VK_NULL_HANDLE) {
        auto vkDestroyAccelerationStructureKHR =
            (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(
                device_->logical_device(), "vkDestroyAccelerationStructureKHR");
        if (vkDestroyAccelerationStructureKHR)
            vkDestroyAccelerationStructureKHR(device_->logical_device(), blas_, nullptr);
    }
    device_->free_buffer(blas_buf_);
}

void VkMesh::init_build_input() noexcept {
    geom_ = {};
    geom_.sType                                  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom_.geometryType                           = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom_.flags                                  = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geom_.geometry.triangles.sType               = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geom_.geometry.triangles.vertexFormat        = VK_FORMAT_R32G32B32_SFLOAT;
    geom_.geometry.triangles.vertexData.deviceAddress = params_.vert_handle + params_.vert_offset;
    geom_.geometry.triangles.vertexStride        = params_.vert_stride;
    geom_.geometry.triangles.maxVertex           = params_.vert_num > 0u ? params_.vert_num - 1u : 0u;
    geom_.geometry.triangles.indexType           = params_.tri_stride == 2u
                                                       ? VK_INDEX_TYPE_UINT16
                                                       : VK_INDEX_TYPE_UINT32;
    geom_.geometry.triangles.indexData.deviceAddress = params_.tri_handle + params_.tri_offset;

    build_info_ = {};
    build_info_.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build_info_.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build_info_.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build_info_.geometryCount = 1u;
    build_info_.pGeometries   = &geom_;
}

void VkMesh::build_bvh(const BLASBuildCommand *cmd) noexcept {
    init_build_input();

    auto vkGetAccelerationStructureBuildSizesKHR =
        (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(
            device_->logical_device(), "vkGetAccelerationStructureBuildSizesKHR");
    auto vkCreateAccelerationStructureKHR =
        (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(
            device_->logical_device(), "vkCreateAccelerationStructureKHR");
    auto vkGetAccelerationStructureDeviceAddressKHR =
        (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(
            device_->logical_device(), "vkGetAccelerationStructureDeviceAddressKHR");
    auto vkCmdBuildAccelerationStructuresKHR =
        (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(
            device_->logical_device(), "vkCmdBuildAccelerationStructuresKHR");

    uint32_t prim_count = params_.tri_num;
    VkAccelerationStructureBuildSizesInfoKHR size_info{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(
        device_->logical_device(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &build_info_, &prim_count, &size_info);

    blas_buf_ = device_->allocate_buffer(
        size_info.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        false, "blas_storage");

    VkAccelerationStructureCreateInfoKHR as_ci{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    as_ci.buffer = blas_buf_.buffer;
    as_ci.size   = size_info.accelerationStructureSize;
    as_ci.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    OC_VK_CHECK(vkCreateAccelerationStructureKHR(device_->logical_device(), &as_ci, nullptr, &blas_));

    VkAccelerationStructureDeviceAddressInfoKHR addr_info{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    addr_info.accelerationStructure = blas_;
    blas_address_ = vkGetAccelerationStructureDeviceAddressKHR(device_->logical_device(), &addr_info);

    VkBufferAllocation scratch = device_->allocate_buffer(
        size_info.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        false, "blas_scratch");

    build_info_.mode                    = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build_info_.dstAccelerationStructure = blas_;
    build_info_.scratchData.deviceAddress = scratch.address;

    VkAccelerationStructureBuildRangeInfoKHR range{prim_count, 0u, 0u, 0u};
    const VkAccelerationStructureBuildRangeInfoKHR *p_range = &range;

    device_->immediate_submit([&](VkCommandBuffer cb) {
        vkCmdBuildAccelerationStructuresKHR(cb, 1u, &build_info_, &p_range);
    });

    device_->free_buffer(scratch);
}

}// namespace ocarina
