// Include the public header twice to check its include guard.
#include "Horizon.h"
#include "Horizon.h"

#include <type_traits>

static_assert(std::is_constructible_v<BufferCopyCommand, const HardwareBuffer&, const HardwareBuffer&>);
static_assert(std::is_constructible_v<BufferToImageCommand, const HardwareBuffer&, const HardwareImage&>);
static_assert(std::is_constructible_v<ImageCopyCommand, const HardwareImage&, const HardwareImage&>);
static_assert(std::is_constructible_v<ImageToBufferCommand, const HardwareImage&, const HardwareBuffer&>);

static_assert(std::is_same_v<decltype(HardwareBuffer{}.getBufferID()), std::uintptr_t>);
static_assert(std::is_copy_constructible_v<HardwareBuffer>);
static_assert(std::is_nothrow_move_constructible_v<HardwareImage>);
static_assert(std::is_assignable_v<EmbeddedShader::BindingKey&, HardwareBuffer&>);
static_assert(!std::is_constructible_v<ComputePipelineBase, const std::vector<uint32_t>&>);
static_assert(!std::is_constructible_v<RasterizerPipelineBase, std::vector<uint32_t>, std::vector<uint32_t>>);

void check_legacy_public_headers() {}
