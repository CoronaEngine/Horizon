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

struct ShaderInterfaceSlot
{
    const horizon::core::Type *type{};
    uint32_t root_index{};
    horizon::core::vector<uint32_t> member_path;
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
