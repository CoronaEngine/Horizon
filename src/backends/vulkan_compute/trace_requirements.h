#pragma once

#include "ast/function_requirements.h"

namespace ocarina {

[[nodiscard]] inline bool native_trace_compatible(const FunctionRequirements &requirements) noexcept {
    return !requirements.requires_compute_workgroups && !requirements.uses_warp &&
           !requirements.uses_subgroup && !requirements.has_native_source;
}

}// namespace ocarina
