#include "backends/vulkan_compute/vk_trace_capabilities.h"
#include "rhi/shader_compile_options.h"

#include <iostream>

namespace {

using namespace ocarina;

constexpr VulkanTraceExtensions all_extensions{true, true, true, true};
constexpr VulkanTraceFeatures all_features{true, true, true, true};
constexpr VulkanTraceFunctions all_functions{true, true};

struct TestCase {
    const char *name;
    VulkanTraceExtensions extensions;
    VulkanTraceFeatures features;
    VulkanTraceFunctions functions;
    VulkanTraceFeatures expected_supported;
    VulkanTraceFeatures expected_enabled;
};

constexpr TestCase cases[]{
    {"neither path", {}, {}, {}, {}, {}},
    {"query only", {true, true, false, true}, all_features, all_functions,
        {true, true, false, false}, {true, true, false, false}},
    {"pipeline only", {true, false, true, true}, all_features, all_functions,
        {true, false, true, true}, {true, false, true, true}},
    {"both paths", all_extensions, all_features, all_functions, all_features, all_features},
    {"AS only", {true, false, false, true}, all_features, all_functions,
        {true, false, false, false}, {true, false, false, false}},
    {"AS extension missing", {false, true, true, true}, all_features, all_functions, {}, {}},
    {"deferred operations extension missing", {true, true, true, false}, all_features, all_functions, {}, {}},
    {"AS feature disabled", all_extensions, {false, true, true, true}, all_functions, {}, {}},
    {"query feature disabled", all_extensions, {true, false, true, true}, all_functions,
        {true, false, true, true}, {true, false, true, true}},
    {"pipeline feature disabled", all_extensions, {true, true, false, true}, all_functions,
        {true, true, false, false}, {true, true, false, false}},
    {"primitive culling feature disabled", all_extensions, {true, true, true, false}, all_functions,
        {true, true, true, false}, {true, true, true, false}},
    {"AS function missing", all_extensions, all_features, {false, true}, all_features, {}},
    {"pipeline function missing", all_extensions, all_features, {true, false}, all_features,
        {true, true, false, false}},
};

bool matches(VulkanTraceFeatures actual, VulkanTraceFeatures expected) {
    return actual.acceleration_structure == expected.acceleration_structure &&
           actual.ray_query == expected.ray_query &&
           actual.ray_tracing_pipeline == expected.ray_tracing_pipeline &&
           actual.primitive_culling == expected.primitive_culling;
}

bool check(const TestCase &test) {
    const auto supported = resolve_vulkan_trace_support(test.extensions, test.features);
    const auto enabled = resolve_vulkan_trace_execution(supported, test.functions);
    if (!matches(supported, test.expected_supported) || !matches(enabled, test.expected_enabled)) {
        std::cerr << "Incorrect Vulkan trace capabilities for " << test.name << '\n';
        return false;
    }

    using Error = TraceModeSelectionError;
    const auto capabilities = enabled.trace_capabilities();
    const auto query = select_trace_mode(TraceMode::RayQuery, capabilities, true, true);
    const auto pipeline = select_trace_mode(TraceMode::RayTracingPipeline, capabilities, true, true);
    if (query.error != (test.expected_enabled.ray_query ? Error::None : Error::RayQueryUnavailable) ||
        pipeline.error != (test.expected_enabled.ray_tracing_pipeline ? Error::None : Error::PipelineUnavailable)) {
        std::cerr << "Incorrect forced trace mode diagnostic for " << test.name << '\n';
        return false;
    }
    return true;
}

}// namespace

int main() {
    bool passed = true;
    for (const auto &test : cases) {
        if (!check(test)) passed = false;
    }
    return passed ? 0 : 1;
}
