#pragma once

#include "core/header.h"
#include "core/stl.h"

namespace ocarina {

class Function;

struct FunctionRequirements {
    bool uses_trace{};
    bool requires_compute_workgroups{};
    bool uses_warp{};
    bool has_native_source{}; // A custom header or named native call is present.
    vector<string> diagnostic_sources;
    unordered_set<const Function *> reachable_functions;
};

[[nodiscard]] OC_AST_API FunctionRequirements analyze_function_requirements(const Function &function) noexcept;

}// namespace ocarina
