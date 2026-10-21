#pragma once

#include "ast/function.h"
#include "vk_readonly_parameter_leaves.h"
#include "vk_compute_device.h"

namespace ocarina {

// One ABI decision shared by source generation, the pipeline layout and dispatch.
// Keep the existing BDA/dispatch prefix, and use only the Vulkan-guaranteed 128
// push-constant bytes. The argument pack's trailing DSL dispatch slot is not a
// Vulkan argument and must not affect this decision.
struct VkShaderParameterLayout {
    static constexpr uint32_t params_offset = 0u;
    static constexpr uint32_t dim_offset = 8u;
    // Dispatches larger than a native group-count limit are tiled. The shader
    // sees the original group coordinates, including from nested callables.
    static constexpr uint32_t group_base_offset = 20u;
    static constexpr uint32_t inline_offset = 32u;
    static constexpr uint32_t max_push_bytes = 128u;

    // Only the read-only NRD SharedConstants prefix uses set1; its byte ABI is unchanged.
    static constexpr uint32_t nrd_uniform_bytes = 832u;
    size_t extent{};
    uint32_t uniform_bytes{};

    explicit VkShaderParameterLayout(const Function &function) noexcept {
        auto append = [&](const Variable &arg) {
            extent = mem_offset(extent, VulkanComputeDevice::alignment(arg.type()));
            extent += VulkanComputeDevice::size(arg.type());
        };
        for (const Variable &arg : function.arguments()) append(arg);
        function.for_each_captured_resource([&](const CapturedResource &resource) {
            append(resource.expression()->variable());
        });
        const auto &name = function.description();
        const bool nrd = name.find("NRD-EDSL-REBLUR-") == 0u || name == "REBLUR-EDSL-HistoryFix";
        if (!is_inline() && nrd && !function.arguments().empty()) {
            const Variable &constants = function.arguments().front();
            const Type *type = constants.type();
            if (type->is_structure() && !type->is_param_struct() &&
                type->cname() == "vision::reblur::edsl::SharedConstants" &&
                type->size() == nrd_uniform_bytes && type->members().size() == 72u) {
                VkReadOnlyParameterLeaves reads{function};
                reads.statement(function.body());
                if (reads.complete && reads.candidates.contains(constants.uid()))
                    uniform_bytes = nrd_uniform_bytes;
            }
        }
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
