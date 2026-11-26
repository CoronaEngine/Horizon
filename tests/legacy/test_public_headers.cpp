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
static_assert(std::is_aggregate_v<HardwareImageCreateInfo>);
static_assert(!std::is_constructible_v<EmbeddedShader::Texture2DProxy<ktm::fvec4>, HardwareImage&&>);
static_assert(!std::is_constructible_v<EmbeddedShader::TextureCubeProxy<ktm::fvec4>, HardwareImage&&>);

template<class T>
concept MoveOnlyHandle = std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T> &&
                         !std::is_copy_constructible_v<T> && !std::is_copy_assignable_v<T>;
static_assert(MoveOnlyHandle<HardwareExecutor> && MoveOnlyHandle<HardwareDisplayer> && MoveOnlyHandle<RasterizerPipeline<>>);
static_assert(MoveOnlyHandle<EmbeddedShader::Texture2DProxy<ktm::fvec4>>);
static_assert(MoveOnlyHandle<EmbeddedShader::TextureCubeProxy<ktm::fvec4>>);

template<class Shader>
concept RawPipelineInput = std::is_constructible_v<ComputePipeline<>, const Shader&> ||
                           std::is_constructible_v<RasterizerPipeline<>, const Shader&, const Shader&>;
static_assert(!RawPipelineInput<std::string>);
static_assert(!RawPipelineInput<std::vector<uint32_t>>);
static_assert(!RawPipelineInput<EmbeddedShader::ShaderCodeModule>);
static_assert(!RawPipelineInput<EmbeddedShader::SlangModule>);

template<class R>
concept CanReadBackInto = requires(const HardwareBuffer& buffer, R&& output) {
    { buffer.copyToData(std::forward<R>(output)) } -> std::same_as<bool>;
};
static_assert(CanReadBackInto<std::vector<uint16_t>&> && CanReadBackInto<std::span<std::byte>> &&
              CanReadBackInto<uint32_t(&)[4]>);
static_assert(!CanReadBackInto<std::span<const uint16_t>> && !CanReadBackInto<std::span<volatile uint16_t>> &&
              !CanReadBackInto<std::vector<std::string>&> && !CanReadBackInto<std::vector<bool>&> &&
              !CanReadBackInto<std::vector<uint16_t>>);

template<class... Args>
concept CanDrawIndirect = requires(RasterBatch& batch, const HardwareBuffer& buffer, Args&&... args) {
    batch.drawIndirect(buffer, buffer, buffer, 1, std::forward<Args>(args)...);
};
static_assert(CanDrawIndirect<> && CanDrawIndirect<const HardwareBuffer&>);
static_assert(!CanDrawIndirect<std::span<const float>>);
static_assert(!CanDrawIndirect<std::size_t, void (*)(std::span<std::byte>)>);

struct RasterPipelineAdapter {
    operator RasterizerPipeline<>() const;
};

struct ComputePipelineAdapter {
    operator ComputePipeline<>() const;
};

template<class T>
concept DeducesPipeline = requires(T value) { ComputePipeline(value); } || requires(T value) { RasterizerPipeline(value); };
static_assert(!DeducesPipeline<ComputePipelineAdapter> && !DeducesPipeline<RasterPipelineAdapter>);
static_assert(std::same_as<decltype(ComputePipeline<>(ComputePipelineAdapter{})), ComputePipeline<>>);
static_assert(requires(decltype([] {}) shader, const std::source_location& location) {
    { ComputePipeline{} } -> std::same_as<ComputePipeline<>>;
    { ComputePipeline(shader) } -> std::same_as<ComputePipeline<>>;
    { ComputePipeline(shader, {4, 2, 1}, {.enableMatrixColumnMajor = true}, location) }
        -> std::same_as<ComputePipeline<>>;
});

static_assert(requires(RasterizerPipeline<>& pipeline, RasterPipelineAdapter adapter) {
    { RasterizerPipeline{} } -> std::same_as<RasterizerPipeline<>>;
    { RasterizerPipeline(std::move(pipeline)) } -> std::same_as<RasterizerPipeline<>>;
    { RasterizerPipeline<>(adapter) } -> std::same_as<RasterizerPipeline<>>;
});

using ShaderVector = EmbeddedShader::VariateProxy<ktm::fvec4>;
static_assert(requires(decltype([](ShaderVector v) { return v; }) vertex,
                       decltype([](ShaderVector) {}) fragment, EmbeddedShader::CompilerOption options,
                       const std::source_location& location) {
    { RasterizerPipeline(vertex, fragment, {.enableMatrixColumnMajor = true}) } -> std::same_as<RasterizerPipeline<>>;
    { RasterizerPipeline(vertex, fragment, options) } -> std::same_as<RasterizerPipeline<>>;
    { RasterizerPipeline(vertex, fragment, {}, 2, location) } -> std::same_as<RasterizerPipeline<>>;
});

template<class T>
concept CanEnqueue = requires(HardwareExecutor& executor, T& value) { executor << value; };
static_assert(!CanEnqueue<HardwareExecutor>);
static_assert(requires(HardwareExecutor& a, HardwareExecutor& b, const CopyCommand& copy) {
    { b.wait(a) << copy } -> std::same_as<HardwareExecutor&>;
});

void check_legacy_public_headers() {}

#include "Codegen/ControlFlows.h"
#include SLANG(compute_write.slang)

using NamedCompute = ComputePipeline<compute_write_slang_t>;
template<class T>
concept HasOutputs = requires(T& value) { value.outputs; };
static_assert(!HasOutputs<compute_write_slang_t>);
static_assert(MoveOnlyHandle<NamedCompute>);
static_assert(requires(NamedCompute& pipeline, HardwareExecutor& executor, HardwareBuffer& buffer,
                       std::span<const ResourceUse> resources) {
    { NamedCompute{} } -> std::same_as<NamedCompute>;
    { ComputePipeline(compute_write_slang) } -> std::same_as<NamedCompute>;
    pipeline.outputs = buffer;
    { pipeline.uses(resources)(1, 2, 3) } -> std::same_as<NamedCompute&>;
    { executor << pipeline.uses({{buffer, ResourceAccess::Write}})(1) } -> std::same_as<HardwareExecutor&>;
});

#include SLANG(examples/Disney/Slang/disney_frag.slang)

template<class Stage>
concept CompleteStage = requires { sizeof(Stage); };
struct drawstress_frag_slang_t;
static_assert(!CompleteStage<drawstress_frag_slang_t>);

#include SLANG(examples/DrawStress/Slang/drawstress_frag.slang)

struct disney_vert_slang_t;
static_assert(!CompleteStage<disney_vert_slang_t>);

#include SLANG(examples/Disney/Slang/disney_vert.slang)

using DisneyRaster = RasterizerPipeline<disney_vert_slang_t, disney_frag_slang_t>;
using SharedVertexRaster = decltype(RasterizerPipeline(disney_vert_slang, drawstress_frag_slang));
template<class T>
concept HasOutColor = requires(T& value) { value.outColor; };
static_assert(!HasOutColor<disney_frag_slang_t> && HasOutColor<DisneyRaster>);
static_assert(requires(DisneyRaster& pipeline) {
    { DisneyRaster{} } -> std::same_as<DisneyRaster>;
    { RasterizerPipeline(disney_vert_slang, disney_frag_slang, 2) } -> std::same_as<DisneyRaster>;
    pipeline.vsp.proj_view;
});
static_assert(requires(SharedVertexRaster& pipeline) { pipeline.vsp.proj_view; pipeline.outColor; });
static_assert(!std::same_as<DisneyRaster, SharedVertexRaster>);

#include "Disney/Slang/disney.pipeline.hpp"
