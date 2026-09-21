#pragma once

#include "core/header.h"
#include "core/stl.h"

namespace horizon::core
{
class Type;
}

namespace horizon::ast
{

enum class Interpolation : uint8_t
{
    None,
    Smooth,
    Flat
};

/// A leaf slot in the recursively flattened shader interface. A vector occupies one slot.
/// Example: VertexInput { float3 position; float2 uv; }, with VS parameters (VertexInput vertex, float4 color):
///   Input member      root_index    member_path    location
///   vertex.position   0             {0}            0
///   vertex.uv         0             {1}            1
///   color             1             {}             2
/// root_index and member_path identify the original parameter and member; location identifies the flattened slot.
struct ShaderInterfaceSlot
{
    const horizon::core::Type *type{};
    /// Zero-based index of the top-level input parameter; always 0 for the single return value of an output slot.
    uint32_t root_index{};
    /// Member indices from the top-level parameter or return value to the leaf, in registered member order.
    /// Empty when the root is not a struct.
    horizon::core::vector<uint32_t> member_path;
    /// Logical slot index, starting at 0 independently for each stage's inputs and outputs, in flattened order.
    /// Used to match VS output and FS input layouts; not a buffer index or byte offset.
    /// Physical resource binding is not implemented yet. Builtins are tracked separately and do not occupy locations.
    uint32_t location{};
    Interpolation interpolation{Interpolation::None};
};

struct ShaderInterface
{
    horizon::core::vector<ShaderInterfaceSlot> inputs;
    horizon::core::vector<ShaderInterfaceSlot> outputs;
};

class Function;
struct RasterDiagnostic;

namespace detail
{
[[nodiscard]] OC_AST_API ShaderInterface build_shader_interface(const Function &function,
                                                                horizon::core::vector<RasterDiagnostic> &diagnostics);
}  // namespace detail

}  // namespace horizon::ast
