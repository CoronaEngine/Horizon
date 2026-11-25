#pragma once

#include "rhi/shader_compile_options.h"

namespace ocarina {

struct VulkanTraceExtensions {
    bool acceleration_structure = false;
    bool ray_query = false;
    bool ray_tracing_pipeline = false;
    bool deferred_host_operations = false;
};

struct VulkanTraceFeatures {
    bool acceleration_structure = false;
    bool ray_query = false;
    bool ray_tracing_pipeline = false;
    bool primitive_culling = false;

    [[nodiscard]] constexpr TraceCapabilities trace_capabilities() const noexcept {
        return {ray_query, ray_tracing_pipeline};
    }
};

struct VulkanTraceFunctions {
    bool acceleration_structure = false;
    bool ray_tracing_pipeline = false;
};

[[nodiscard]] constexpr VulkanTraceFeatures resolve_vulkan_trace_support(
    VulkanTraceExtensions extensions, VulkanTraceFeatures features) noexcept {
    const bool acceleration_structure = extensions.acceleration_structure &&
        extensions.deferred_host_operations && features.acceleration_structure;
    const bool pipeline = acceleration_structure && extensions.ray_tracing_pipeline && features.ray_tracing_pipeline;
    return {
        acceleration_structure,
        acceleration_structure && extensions.ray_query && features.ray_query,
        pipeline,
        pipeline && features.primitive_culling,
    };
}

[[nodiscard]] constexpr VulkanTraceFeatures resolve_vulkan_trace_execution(
    VulkanTraceFeatures enabled, VulkanTraceFunctions functions) noexcept {
    const bool acceleration_structure = enabled.acceleration_structure && functions.acceleration_structure;
    const bool pipeline = acceleration_structure && enabled.ray_tracing_pipeline && functions.ray_tracing_pipeline;
    return {
        acceleration_structure,
        acceleration_structure && enabled.ray_query,
        pipeline,
        pipeline && enabled.primitive_culling,
    };
}

}// namespace ocarina
