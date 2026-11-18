#pragma once

namespace ocarina {

enum class TraceMode {
    Auto,
    RayQuery,
    RayTracingPipeline,
};

struct ShaderCompileOptions {
    TraceMode trace_mode = TraceMode::Auto;
};

enum class ShaderExecutionMode {
    Compute,
    ComputeRayQuery,
    RayTracingPipeline,
};

struct TraceCapabilities {
    bool ray_query = false;
    bool ray_tracing_pipeline = false;
};

enum class TraceModeSelectionError {
    None,
    InvalidMode,
    NoSupportedPath,
    RayQueryUnavailable,
    PipelineUnavailable,
    NativeIncompatible,
};

struct TraceModeSelection {
    // Meaningful only when error is None.
    ShaderExecutionMode execution_mode;
    TraceModeSelectionError error;
};

[[nodiscard]] constexpr TraceModeSelection select_trace_mode(
    TraceMode request, TraceCapabilities capabilities, bool uses_trace, bool native_compatible) noexcept {
    using Mode = ShaderExecutionMode;
    using Error = TraceModeSelectionError;

    if (request != TraceMode::Auto && request != TraceMode::RayQuery &&
        request != TraceMode::RayTracingPipeline) {
        return {Mode::Compute, Error::InvalidMode};
    }
    if (!uses_trace) return {Mode::Compute, Error::None};

    if (request == TraceMode::RayQuery) {
        if (!capabilities.ray_query) return {Mode::Compute, Error::RayQueryUnavailable};
        return {Mode::ComputeRayQuery, Error::None};
    }
    if (request == TraceMode::Auto) {
        if (capabilities.ray_query) return {Mode::ComputeRayQuery, Error::None};
        if (!capabilities.ray_tracing_pipeline) return {Mode::Compute, Error::NoSupportedPath};
    }
    if (!native_compatible) return {Mode::Compute, Error::NativeIncompatible};
    if (!capabilities.ray_tracing_pipeline) return {Mode::Compute, Error::PipelineUnavailable};
    return {Mode::RayTracingPipeline, Error::None};
}

}// namespace ocarina
