//
// Created during CUDA→Slang+Vulkan migration.
// Replaces cuda_mesh.cpp — Vulkan BLAS build.
//

#include "vk_mesh.h"
#include "vk_compute_device.h"
#include "rhi/command.h"
#include "util.h"

namespace ocarina {

void compact_acceleration_structure(VulkanComputeDevice *device, VkAccelerationStructureTypeKHR type,
                                    VkAccelerationStructureKHR &accel, VkBufferAllocation &storage,
                                    VkDeviceAddress &address) noexcept {
    auto logical = device->logical_device();
    auto write_properties = reinterpret_cast<PFN_vkCmdWriteAccelerationStructuresPropertiesKHR>(
        vkGetDeviceProcAddr(logical, "vkCmdWriteAccelerationStructuresPropertiesKHR"));
    auto create_as = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
        vkGetDeviceProcAddr(logical, "vkCreateAccelerationStructureKHR"));
    auto copy_as = reinterpret_cast<PFN_vkCmdCopyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(logical, "vkCmdCopyAccelerationStructureKHR"));
    auto destroy_as = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(logical, "vkDestroyAccelerationStructureKHR"));
    auto get_address = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
        vkGetDeviceProcAddr(logical, "vkGetAccelerationStructureDeviceAddressKHR"));

    VkQueryPoolCreateInfo query_info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    query_info.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
    query_info.queryCount = 1u;
    VkQueryPool query = VK_NULL_HANDLE;
    OC_VK_CHECK(vkCreateQueryPool(logical, &query_info, nullptr, &query));
    device->immediate_submit([&](VkCommandBuffer cb) {
        vkCmdResetQueryPool(cb, query, 0u, 1u);
        write_properties(cb, 1u, &accel, query_info.queryType, query, 0u);
    });
    VkDeviceSize compacted_size = 0;
    OC_VK_CHECK(vkGetQueryPoolResults(logical, query, 0u, 1u, sizeof(compacted_size),
        &compacted_size, sizeof(compacted_size), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
    vkDestroyQueryPool(logical, query, nullptr);
    OC_INFO_FORMAT("Vulkan AS type {}: storage={} bytes, compacted={} bytes", uint(type), storage.size, compacted_size);
    if (!compacted_size || compacted_size >= storage.size) return;

    auto compacted_storage = device->allocate_buffer(compacted_size,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        false, "compacted_acceleration_structure");
    VkAccelerationStructureCreateInfoKHR create_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    create_info.type = type;
    create_info.buffer = compacted_storage.buffer;
    create_info.size = compacted_size;
    VkAccelerationStructureKHR compacted = VK_NULL_HANDLE;
    OC_VK_CHECK(create_as(logical, &create_info, nullptr, &compacted));
    VkCopyAccelerationStructureInfoKHR copy_info{VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
    copy_info.src = accel;
    copy_info.dst = compacted;
    copy_info.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
    device->immediate_submit([&](VkCommandBuffer cb) { copy_as(cb, &copy_info); });
    destroy_as(logical, accel, nullptr);
    device->free_buffer(storage);
    accel = compacted;
    storage = compacted_storage;
    VkAccelerationStructureDeviceAddressInfoKHR address_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    address_info.accelerationStructure = accel;
    address = get_address(logical, &address_info);
}

VkMesh::~VkMesh() noexcept {
    clear();
}

void VkMesh::clear() noexcept {
    if (blas_ != VK_NULL_HANDLE) {
        auto vkDestroyAccelerationStructureKHR =
            (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(
                device_->logical_device(), "vkDestroyAccelerationStructureKHR");
        if (vkDestroyAccelerationStructureKHR)
            vkDestroyAccelerationStructureKHR(device_->logical_device(), blas_, nullptr);
    }
    device_->free_buffer(blas_buf_);
    device_->free_buffer(packed_indices_);
    blas_ = VK_NULL_HANDLE;
    blas_address_ = 0;
}

void VkMesh::init_build_input() noexcept {
    geom_ = {};
    geom_.sType                                  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom_.geometryType                           = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom_.flags = (params_.geom_tag & DISABLE_ANYHIT ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0u) |
                  (params_.geom_tag & SINGLE_ANYHIT_CALL ? VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR : 0u);
    geom_.geometry.triangles.sType               = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geom_.geometry.triangles.vertexFormat        = VK_FORMAT_R32G32B32_SFLOAT;
    geom_.geometry.triangles.vertexData.deviceAddress = params_.vert_handle + params_.vert_offset;
    geom_.geometry.triangles.vertexStride        = params_.vert_stride;
    geom_.geometry.triangles.maxVertex           = params_.vert_num > 0u ? params_.vert_num - 1u : 0u;
    geom_.geometry.triangles.indexType           = VK_INDEX_TYPE_UINT32;
    geom_.geometry.triangles.indexData.deviceAddress = params_.tri_handle + params_.tri_offset;

    build_info_ = {};
    build_info_.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build_info_.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build_info_.flags = params_.usage_tag == FAST_BUILD ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR :
                                                        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build_info_.flags |= params_.usage_tag == FAST_UPDATE ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR :
                                                          VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
    build_info_.geometryCount = 1u;
    build_info_.pGeometries   = &geom_;
}

void VkMesh::build_bvh(const BLASBuildCommand *cmd) noexcept {
    clear();
    init_build_input();
    // OptiX accepts a stride per uint32 index triplet; Vulkan requires tightly
    // packed indices, so remove any padding in the shared mesh representation.
    constexpr VkDeviceSize index_bytes = 3 * sizeof(uint32_t);
    OC_ERROR_IF(params_.tri_stride < index_bytes, "Triangle indices require three uint32 values");
    if (params_.tri_stride != index_bytes && params_.tri_num != 0) {
        packed_indices_ = device_->allocate_buffer(params_.tri_num * index_bytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, "packed_blas_indices");
        VkDeviceSize offset = 0;
        auto src = device_->get_vk_buffer(params_.tri_handle, &offset);
        vector<VkBufferCopy> regions(params_.tri_num);
        for (uint i = 0; i < params_.tri_num; ++i)
            regions[i] = {offset + params_.tri_offset + i * params_.tri_stride, i * index_bytes, index_bytes};
        device_->immediate_submit([&](VkCommandBuffer cb) {
            vkCmdCopyBuffer(cb, src, packed_indices_.buffer, regions.size(), regions.data());
        });
        geom_.geometry.triangles.indexData.deviceAddress = packed_indices_.address;
    }

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
        false, "blas_scratch", device_->scratch_alignment());

    build_info_.mode                    = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build_info_.dstAccelerationStructure = blas_;
    build_info_.scratchData.deviceAddress = scratch.address;

    VkAccelerationStructureBuildRangeInfoKHR range{prim_count, 0u, 0u, 0u};
    const VkAccelerationStructureBuildRangeInfoKHR *p_range = &range;

    device_->immediate_submit([&](VkCommandBuffer cb) {
        vkCmdBuildAccelerationStructuresKHR(cb, 1u, &build_info_, &p_range);
    });

    device_->free_buffer(scratch);
    if (params_.usage_tag != FAST_UPDATE) {
        compact_acceleration_structure(device_, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                                       blas_, blas_buf_, blas_address_);
        build_info_.dstAccelerationStructure = blas_;
    }
}

}// namespace ocarina
