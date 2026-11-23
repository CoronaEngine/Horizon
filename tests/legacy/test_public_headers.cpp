// Include the public header twice to check its include guard.
#include "Horizon.h"
#include "Horizon.h"

#include <type_traits>

static_assert(requires(const HardwareBuffer& buffer, const HardwareImage& image) {
    { buffer.copyTo(buffer) } -> std::same_as<CopyCommand>;
    { buffer.copyTo(image) } -> std::same_as<CopyCommand>;
    { image.copyTo(image) } -> std::same_as<CopyCommand>;
    { image.copyTo(buffer) } -> std::same_as<CopyCommand>;
    { image.copyFrom(nullptr) } -> std::same_as<CopyCommand>;
});

static_assert(std::is_same_v<decltype(HardwareBuffer{}.getBufferID()), std::uintptr_t>);
static_assert(std::is_copy_constructible_v<HardwareBuffer>);
static_assert(std::is_nothrow_move_constructible_v<HardwareImage>);
static_assert(std::is_assignable_v<EmbeddedShader::BindingKey&, HardwareBuffer&>);
static_assert(!std::is_constructible_v<ComputePipeline<>, const std::vector<uint32_t>&>);
static_assert(!std::is_constructible_v<RasterizerPipeline<>, std::vector<uint32_t>, std::vector<uint32_t>>);

using EmbeddedShader::ShaderLanguage;
static_assert(!std::is_constructible_v<ComputePipeline<>, std::string, ShaderLanguage>);
static_assert(!std::is_constructible_v<ComputePipeline<>, std::string, ShaderLanguage, std::source_location>);
static_assert(!std::is_constructible_v<RasterizerPipeline<>, std::string, std::string, uint32_t, ShaderLanguage>);
static_assert(!std::is_constructible_v<RasterizerPipeline<>, std::string, std::string, uint32_t, ShaderLanguage, ShaderLanguage>);
static_assert(!std::is_constructible_v<RasterizerPipeline<>, std::string, std::string, uint32_t,
                                       ShaderLanguage, ShaderLanguage, std::source_location>);

static_assert(requires(const std::string& source, const std::source_location& location) {
    { ComputePipeline(source) } -> std::same_as<ComputePipeline<>>;
    { ComputePipeline(source, location) } -> std::same_as<ComputePipeline<>>;
    { RasterizerPipeline(source, source) } -> std::same_as<RasterizerPipeline<>>;
    { RasterizerPipeline(source, source, 1, location) } -> std::same_as<RasterizerPipeline<>>;
});

template<class T>
concept CanEnqueue = requires(HardwareExecutor& executor, T& value) { executor << value; };
static_assert(!CanEnqueue<HardwareExecutor>);
static_assert(requires(HardwareExecutor& a, HardwareExecutor& b, const CopyCommand& copy) {
    { b.wait(a) << copy } -> std::same_as<HardwareExecutor&>;
});

void check_legacy_public_headers() {}
