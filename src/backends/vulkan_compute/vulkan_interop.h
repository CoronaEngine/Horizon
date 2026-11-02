#pragma once

#include "rhi/native_interop.h"
#include <vulkan/vulkan.h>

namespace ocarina {

// Borrowed handles for submitting compute work to the engine's existing queue.
// Native callbacks may submit command buffers, but must not re-enter this
// Ocarina stream, wait for it, or destroy the borrowed device/queue/resources.
// Access to queue submission is serialized with Ocarina for the callback.
struct VulkanInteropContext {
    VkInstance instance{};
    VkPhysicalDevice physical_device{};
    VkDevice device{};
    VkQueue compute_queue{};
    uint32_t queue_family{};
    PFN_vkGetInstanceProcAddr get_instance_proc_addr{};

    // Convert an Ocarina buffer device address to its allocation and offset.
    // The buffer remains owned by Ocarina; retain its owner in the callback's
    // captures if its lifetime must extend through the native GPU submission.
    void *backend{};
    VkBuffer (*resolve_buffer)(void *backend, uint64_t address, VkDeviceSize *offset){};
};

}// namespace ocarina
