#include "rhi/shader_compile_options.h"

#include <array>
#include <iostream>

namespace {

using namespace ocarina;
using enum TraceModeSelectionError;

constexpr TraceModeSelection compute{ShaderExecutionMode::Compute, None};
constexpr TraceModeSelection query{ShaderExecutionMode::ComputeRayQuery, None};
constexpr TraceModeSelection pipeline{ShaderExecutionMode::RayTracingPipeline, None};
constexpr TraceModeSelection no_path{ShaderExecutionMode::Compute, NoSupportedPath};
constexpr TraceModeSelection no_query{ShaderExecutionMode::Compute, RayQueryUnavailable};
constexpr TraceModeSelection no_pipeline{ShaderExecutionMode::Compute, PipelineUnavailable};
constexpr TraceModeSelection incompatible{ShaderExecutionMode::Compute, NativeIncompatible};

constexpr TraceCapabilities neither{};
constexpr TraceCapabilities query_only{true, false};
constexpr TraceCapabilities pipeline_only{false, true};
constexpr TraceCapabilities both{true, true};

struct TestCase {
    bool uses_trace;
    bool native_compatible;
    TraceCapabilities capabilities;
    std::array<TraceModeSelection, 3> expected;
};

constexpr std::array requests{TraceMode::Auto, TraceMode::RayQuery, TraceMode::RayTracingPipeline};
constexpr TestCase cases[] = {
    // Results for Auto, RayQuery, and RayTracingPipeline in that order.
    {false, true, neither, {compute, compute, compute}},
    {false, true, query_only, {compute, compute, compute}},
    {false, true, pipeline_only, {compute, compute, compute}},
    {false, true, both, {compute, compute, compute}},
    {false, false, neither, {compute, compute, compute}},
    {true, true, neither, {no_path, no_query, no_pipeline}},
    {true, true, query_only, {query, query, no_pipeline}},
    {true, true, pipeline_only, {pipeline, no_query, pipeline}},
    {true, true, both, {query, query, pipeline}},
    {true, false, neither, {no_path, no_query, incompatible}},
    {true, false, query_only, {query, query, incompatible}},
    {true, false, pipeline_only, {incompatible, no_query, incompatible}},
    {true, false, both, {query, query, incompatible}},
};

static_assert(ShaderCompileOptions{}.trace_mode == TraceMode::Auto);
static_assert(select_trace_mode(TraceMode::Auto, both, true, true).execution_mode ==
              ShaderExecutionMode::ComputeRayQuery);

bool check_selection(const TestCase &test, TraceMode request, TraceModeSelection expected) {
    const auto result = select_trace_mode(request, test.capabilities, test.uses_trace, test.native_compatible);
    if (result.error == expected.error && (expected.error != None || result.execution_mode == expected.execution_mode)) {
        return true;
    }

    std::cerr << "Unexpected trace selection for request " << static_cast<int>(request)
              << ", uses trace " << test.uses_trace << ", native compatible " << test.native_compatible
              << ", ray query " << test.capabilities.ray_query
              << ", ray tracing pipeline " << test.capabilities.ray_tracing_pipeline
              << " (error " << static_cast<int>(result.error) << ", expected " << static_cast<int>(expected.error);
    if (result.error == None && expected.error == None) {
        std::cerr << ", mode " << static_cast<int>(result.execution_mode)
                  << ", expected " << static_cast<int>(expected.execution_mode);
    }
    std::cerr << ")\n";
    return false;
}

}// namespace

int main() {
    int failures = 0;
    for (const auto &test : cases) {
        for (std::size_t i = 0; i < requests.size(); ++i) {
            if (!check_selection(test, requests[i], test.expected[i])) ++failures;
        }
        if (!check_selection(test, static_cast<TraceMode>(-1), {ShaderExecutionMode::Compute, InvalidMode})) {
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}
