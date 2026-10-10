//
// Created during CUDA→Slang+Vulkan migration.
// Replaces optix_accel.cpp — Vulkan TLAS build/update.
//

#include "vk_accel.h"
#include "vk_compute_device.h"
#include "vk_command_visitor.h"
#include "vk_mesh.h"
#include "util.h"

namespace ocarina {

VkBuildAccelerationStructureFlagsKHR VkAccel::vk_build_flags() const noexcept {
    switch (usage_tag_) {
        case FAST_BUILD:
            return VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
        case FAST_UPDATE:
            return VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR |
                   VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        case FAST_TRACE:
        default:
            return VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR |
                   VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    }
}

VkAccel::~VkAccel() noexcept {
    clear();
    device_->free_buffer(instances_buf_);
}

void VkAccel::clear() noexcept {
    if (tlas_ != VK_NULL_HANDLE) {
        auto fn = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(
            device_->logical_device(), "vkDestroyAccelerationStructureKHR");
        if (fn) fn(device_->logical_device(), tlas_, nullptr);
        tlas_ = VK_NULL_HANDLE;
    }
    device_->free_buffer(tlas_buf_);
    tlas_address_ = 0;
    built_        = false;
    built_blas_.clear();
    device_->free_buffer(instances_buf_);
    Accel::Impl::clear();
}

void VkAccel::reallocate_tlas(uint32_t instance_count) noexcept {
    auto vkGetAS = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(
        device_->logical_device(), "vkGetAccelerationStructureBuildSizesKHR");
    auto vkCreateAS = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(
        device_->logical_device(), "vkCreateAccelerationStructureKHR");
    auto vkGetAddr = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(
        device_->logical_device(), "vkGetAccelerationStructureDeviceAddressKHR");

    tlas_geom_ = {};
    tlas_geom_.sType                                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    tlas_geom_.geometryType                          = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tlas_geom_.geometry.instances.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    tlas_geom_.geometry.instances.arrayOfPointers    = VK_FALSE;
    tlas_geom_.geometry.instances.data.deviceAddress = instances_buf_.address;

    build_info_ = {};
    build_info_.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build_info_.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build_info_.flags         = vk_build_flags();
    build_info_.geometryCount = 1u;
    build_info_.pGeometries   = &tlas_geom_;

    VkAccelerationStructureBuildSizesInfoKHR size_info{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAS(device_->logical_device(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &build_info_, &instance_count, &size_info);

    if (tlas_ != VK_NULL_HANDLE) {
        auto fn = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(
            device_->logical_device(), "vkDestroyAccelerationStructureKHR");
        if (fn) fn(device_->logical_device(), tlas_, nullptr);
    }
    device_->free_buffer(tlas_buf_);

    tlas_buf_ = device_->allocate_buffer(
        size_info.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        false, "tlas_storage");

    VkAccelerationStructureCreateInfoKHR ci{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    ci.buffer = tlas_buf_.buffer;
    ci.size   = size_info.accelerationStructureSize;
    ci.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    OC_VK_CHECK(vkCreateAS(device_->logical_device(), &ci, nullptr, &tlas_));

    VkAccelerationStructureDeviceAddressInfoKHR addr{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    addr.accelerationStructure = tlas_;
    tlas_address_ = vkGetAddr(device_->logical_device(), &addr);
}

// ── build_tlas ────────────────────────────────────────────────────────────────

void VkAccel::build_tlas(VkComputeCommandVisitor *visitor) noexcept {
    auto vkCmdBuildAS = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(
        device_->logical_device(), "vkCmdBuildAccelerationStructuresKHR");

    uint32_t instance_count = static_cast<uint32_t>(meshes_.size());
    if (instance_count == 0) { clear(); return; }

    // (Re)allocate instance buffer if needed
    if (instances_buf_.buffer == VK_NULL_HANDLE ||
        built_blas_.size() != instance_count) {
        device_->free_buffer(instances_buf_);
        instances_buf_ = device_->allocate_buffer(
            instance_count * sizeof(VkAccelerationStructureInstanceKHR),
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            true, "tlas_instances");
    }

    // Write instance descriptors into the host-visible instances buffer
    void *mapped = nullptr;
    vmaMapMemory(device_->vma_allocator(), instances_buf_.alloc, &mapped);
    auto *inst_ptr = reinterpret_cast<VkAccelerationStructureInstanceKHR *>(mapped);
    for (uint32_t i = 0; i < instance_count; ++i) {
        auto *mesh_impl = reinterpret_cast<VkMesh *>(meshes_[i].impl());
        VkAccelerationStructureInstanceKHR inst{};
        // Copy row-major 4x3 transform
        const float4x4 &t = transforms_[i];
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c)
                inst.transform.matrix[r][c] = t[c][r];
        inst.instanceCustomIndex                    = i;
        inst.mask                                   = 0xFFu;
        inst.instanceShaderBindingTableRecordOffset = 0u;
        inst.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        inst.accelerationStructureReference        = mesh_impl->blas_handle();
        inst_ptr[i] = inst;
    }
    vmaUnmapMemory(device_->vma_allocator(), instances_buf_.alloc);

    reallocate_tlas(instance_count);

    // Scratch buffer
    VkAccelerationStructureBuildSizesInfoKHR size_info{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    auto vkGetAS = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(
        device_->logical_device(), "vkGetAccelerationStructureBuildSizesKHR");
    vkGetAS(device_->logical_device(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &build_info_, &instance_count, &size_info);

    VkBufferAllocation scratch = device_->allocate_buffer(
        size_info.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        false, "tlas_scratch", device_->scratch_alignment());

    build_info_.mode                     = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build_info_.dstAccelerationStructure = tlas_;
    build_info_.scratchData.deviceAddress = scratch.address;

    VkAccelerationStructureBuildRangeInfoKHR range{instance_count, 0u, 0u, 0u};
    const VkAccelerationStructureBuildRangeInfoKHR *p_range = &range;

    device_->immediate_submit([&](VkCommandBuffer cb) {
        vkCmdBuildAS(cb, 1u, &build_info_, &p_range);
    });

    device_->free_buffer(scratch);
    built_ = true;
    built_blas_.clear();
    for (const auto &mesh : meshes_) built_blas_.push_back(mesh.impl()->blas_handle());
    mark_build();
}

// ── update_tlas ───────────────────────────────────────────────────────────────

void VkAccel::update_tlas(VkComputeCommandVisitor *visitor) noexcept {
    if (meshes_.empty()) { clear(); return; }
    bool topology_changed = built_blas_.size() != meshes_.size();
    if (!topology_changed) {
        for (size_t i = 0; i < meshes_.size(); ++i)
            topology_changed |= built_blas_[i] != meshes_[i].impl()->blas_handle();
    }
    if (!built_ || usage_tag_ != FAST_UPDATE || topology_changed) {
        build_tlas(visitor);
        return;
    }

    auto vkCmdBuildAS = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(
        device_->logical_device(), "vkCmdBuildAccelerationStructuresKHR");

    uint32_t instance_count = static_cast<uint32_t>(meshes_.size());

    // Update instance transforms in the existing buffer
    void *mapped = nullptr;
    vmaMapMemory(device_->vma_allocator(), instances_buf_.alloc, &mapped);
    auto *inst_ptr = reinterpret_cast<VkAccelerationStructureInstanceKHR *>(mapped);
    for (uint32_t i = 0; i < instance_count; ++i) {
        const float4x4 &t = transforms_[i];
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c)
                inst_ptr[i].transform.matrix[r][c] = t[c][r];
    }
    vmaUnmapMemory(device_->vma_allocator(), instances_buf_.alloc);

    VkAccelerationStructureBuildSizesInfoKHR size_info{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    auto vkGetAS = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(
        device_->logical_device(), "vkGetAccelerationStructureBuildSizesKHR");
    vkGetAS(device_->logical_device(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &build_info_, &instance_count, &size_info);

    VkBufferAllocation scratch = device_->allocate_buffer(
        size_info.updateScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        false, "tlas_update_scratch", device_->scratch_alignment());

    build_info_.mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
    build_info_.srcAccelerationStructure  = tlas_;
    build_info_.dstAccelerationStructure  = tlas_;
    build_info_.scratchData.deviceAddress = scratch.address;

    VkAccelerationStructureBuildRangeInfoKHR range{instance_count, 0u, 0u, 0u};
    const VkAccelerationStructureBuildRangeInfoKHR *p_range = &range;

    device_->immediate_submit([&](VkCommandBuffer cb) {
        vkCmdBuildAS(cb, 1u, &build_info_, &p_range);
    });

    device_->free_buffer(scratch);
    mark_update();
}

}// namespace ocarina
