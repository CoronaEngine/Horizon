//
// Created during CUDA→Slang+Vulkan migration.
//

#pragma once

#include "core/stl.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace ocarina {

// ── VkResult error check ────────────────────────────────────────────────────

[[nodiscard]] inline const char *vk_result_string(VkResult r) noexcept {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        default: return "VK_UNKNOWN_ERROR";
    }
}

#define OC_VK_CHECK(expr)                                                         \
    do {                                                                           \
        VkResult _oc_vk_result = (expr);                                          \
        OC_ERROR_IF(_oc_vk_result != VK_SUCCESS,                                  \
                    ocarina::format("Vulkan error {} in {}: {}",                  \
                                    ocarina::vk_result_string(_oc_vk_result),     \
                                    #expr, __FILE__));                             \
    } while (false)

// ── Resource allocations ─────────────────────────────────────────────────────

struct VkBufferAllocation {
    VkBuffer    buffer{VK_NULL_HANDLE};
    VmaAllocation alloc{};
    VkDeviceAddress address{0};  // valid only when SHADER_DEVICE_ADDRESS was requested
    VkDeviceSize size{0};
};

struct VkImageAllocation {
    VkImage       image{VK_NULL_HANDLE};
    VmaAllocation alloc{};
    VkImageView   view{VK_NULL_HANDLE};
    VkImageView   storage_view{VK_NULL_HANDLE};  // for write/surface access
};

}// namespace ocarina
