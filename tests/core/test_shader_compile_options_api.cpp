#include "rhi/resources/shader.h"

#include <concepts>
#include <type_traits>
#include <utility>

namespace {

using namespace ocarina;
using TestKernel = Kernel<void()>;
using TestShader = Shader<void()>;

static_assert(requires(const Device &device, const TestKernel &kernel, const string &desc, ShaderCompileOptions options) {
    { device.compile(kernel) } -> std::same_as<TestShader>;
    { device.compile(kernel, desc) } -> std::same_as<TestShader>;
    { device.compile(kernel, {}) } -> std::same_as<TestShader>;
    { device.compile(kernel, desc, CS) } -> std::same_as<TestShader>;
    { device.compile(kernel, desc, CS, options) } -> std::same_as<TestShader>;
    { device.trace_capabilities() } -> std::same_as<TraceCapabilities>;
});

static_assert(requires(const Device &device, TestKernel &&kernel, const string &desc, ShaderCompileOptions options) {
    device.async_compile(std::move(kernel));
    device.async_compile(std::move(kernel), desc);
    device.async_compile(std::move(kernel), {});
    device.async_compile(std::move(kernel), desc, CS);
    { device.async_compile(std::move(kernel), desc, CS, options) }
        -> std::same_as<decltype(device.async_compile(std::move(kernel)))>;
});

static_assert(requires(Device::Impl &device, const Function &function, const string &desc, ShaderCompileOptions options) {
    { device.create_shader(function) } -> std::same_as<handle_ty>;
    { device.create_shader(function, options) } -> std::same_as<handle_ty>;
    { device.create_shader(function, options, desc) } -> std::same_as<handle_ty>;
});

static_assert(requires(Device::Impl *device, shared_ptr<Function> function, const TestShader &shader) {
    TestShader{device, function, CS};
    TestShader{device, function, CS, ShaderCompileOptions{TraceMode::RayQuery}};
    { shader.requested_options() } -> std::same_as<ShaderCompileOptions>;
    { shader.execution_mode() } -> std::same_as<ShaderExecutionMode>;
    shader().dispatch(1u);
});

struct LegacyShaderImpl final : Shader<>::Impl {
    void launch(handle_ty, ShaderDispatchCommand *) noexcept override {}
};

static_assert(!std::is_abstract_v<LegacyShaderImpl>);

}// namespace
