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
static_assert(!std::is_constructible_v<ComputePipelineBase, const std::vector<uint32_t>&>);
static_assert(!std::is_constructible_v<RasterizerPipelineBase, std::vector<uint32_t>, std::vector<uint32_t>>);

void check_legacy_public_headers() {}
