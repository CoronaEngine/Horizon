#pragma once

#include "backends/vulkan_compute/vulkan_interop.h"
#include "core/util/logging.h"
#include "rhi/command.h"
#include "rhi/resources/stream.h"

inline bool test_device_supports_warp32(ocarina::Device &device) {
    VkPhysicalDeviceSubgroupProperties subgroup{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &subgroup};
    bool queried = false;
    auto stream = device.create_stream();
    stream << ocarina::native_callback([&](const ocarina::NativeInteropContext &context) {
        OC_ERROR_IF(context.backend != ocarina::NativeInteropBackend::VulkanCompute || !context.native,
                    "Missing Vulkan context for subgroup property query");
        const auto &vk = *static_cast<const ocarina::VulkanInteropContext *>(context.native);
        auto get_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            vk.get_instance_proc_addr(vk.instance, "vkGetPhysicalDeviceProperties2"));
        OC_ERROR_IF(!get_properties, "Missing Vulkan subgroup property query");
        get_properties(vk.physical_device, &properties);
        queried = true;
    }) << ocarina::synchronize() << ocarina::commit();
    OC_ERROR_IF(!queried, "Vulkan subgroup property query did not run");

    // Match the Vulkan backend's DSL warp32 capability check.
    return subgroup.subgroupSize >= 32u && subgroup.subgroupSize <= 128u &&
           subgroup.subgroupSize % 32u == 0u &&
           (subgroup.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) &&
           (subgroup.supportedOperations & VK_SUBGROUP_FEATURE_BALLOT_BIT);
}
