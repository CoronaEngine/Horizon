// Deliberately include the command header first and twice, without backend headers.
#include "HardwareCommands_old.h"
#include "Horizon_old.h"
#include "HardwareCommands_old.h"

#include <type_traits>

static_assert(std::is_same_v<decltype(HardwareBuffer{}.getBufferID()), std::uintptr_t>);
static_assert(std::is_copy_constructible_v<HardwareBuffer>);
static_assert(std::is_nothrow_move_constructible_v<HardwareImage>);
static_assert(std::is_same_v<Corona::Horizon::HardwareBuffer, ::HardwareBuffer>);
static_assert(!std::is_constructible_v<ComputePipelineBase, const std::vector<uint32_t>&>);
static_assert(!std::is_constructible_v<RasterizerPipelineBase, std::vector<uint32_t>, std::vector<uint32_t>>);

void check_legacy_public_headers() {}
