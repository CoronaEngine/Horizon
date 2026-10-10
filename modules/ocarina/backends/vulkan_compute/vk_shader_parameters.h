#pragma once

#include "ast/function.h"
#include "vk_compute_device.h"

namespace ocarina {

// One ABI decision shared by source generation, the pipeline layout and dispatch.
// Keep the existing BDA/dispatch prefix, and use only the Vulkan-guaranteed 128
// push-constant bytes. The argument pack's trailing CUDA dispatch slot is not a
// Vulkan argument and must not affect this decision.
struct VkShaderParameterLayout {
    static constexpr uint32_t params_offset = 0u;
    static constexpr uint32_t dim_offset = 8u;
    static constexpr uint32_t inline_offset = 20u;
    static constexpr uint32_t max_push_bytes = 128u;

    size_t extent{};

    explicit VkShaderParameterLayout(const Function &function) noexcept {
        auto append = [&](const Variable &arg) {
            extent = mem_offset(extent, VulkanComputeDevice::alignment(arg.type()));
            extent += VulkanComputeDevice::size(arg.type());
        };
        for (const Variable &arg : function.arguments()) append(arg);
        function.for_each_captured_resource([&](const CapturedResource &resource) {
            append(resource.expression()->variable());
        });
    }

    [[nodiscard]] bool is_inline() const noexcept {
        return extent <= max_push_bytes - inline_offset;
    }
    [[nodiscard]] uint32_t inline_word_count() const noexcept {
        return is_inline() ? static_cast<uint32_t>((extent + 3u) / 4u) : 0u;
    }
    [[nodiscard]] uint32_t push_bytes() const noexcept {
        return inline_offset + inline_word_count() * 4u;
    }
};

}// namespace ocarina
