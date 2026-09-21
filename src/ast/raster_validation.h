#pragma once

#include "core/header.h"
#include "core/stl.h"
#include <stdexcept>

namespace horizon::ast
{

class Function;

enum class RasterDiagnosticCode : uint8_t
{
    InvalidStage,
    InvalidInterfaceType,
    InterfaceMismatch,
    InvalidBuiltinStage,
    ReadOnlyWrite,
    InvalidReturn,
    MissingVertexPosition,
    PhysicalResourceCapture,
    RecursiveCall,
    InvalidBuiltinType,
    InvalidBuiltinConfiguration,
    MissingBuiltinWrite
};

struct RasterDiagnostic
{
    RasterDiagnosticCode code;
    horizon::core::string message;
};

class OC_AST_API RasterValidationError : public std::invalid_argument
{
private:
    horizon::core::vector<RasterDiagnostic> diagnostics_;

public:
    explicit RasterValidationError(horizon::core::vector<RasterDiagnostic> diagnostics);
    [[nodiscard]] horizon::core::span<const RasterDiagnostic> diagnostics() const noexcept
    {
        return diagnostics_;
    }
};

[[nodiscard]] OC_AST_API horizon::core::vector<RasterDiagnostic> validate_raster_function(const Function &function);
[[nodiscard]] OC_AST_API horizon::core::vector<RasterDiagnostic> validate_raster_pair(const Function &vertex,
                                                                                      const Function &fragment);

namespace detail
{
OC_AST_API void reset_raster_hashes(const Function &function);
[[nodiscard]] OC_AST_API horizon::core::vector<RasterDiagnostic> validate_raster_call_graph(const Function &function);
[[nodiscard]] OC_AST_API horizon::core::vector<RasterDiagnostic> validate_kernel_raster_usage(const Function &function);
}  // namespace detail

}  // namespace horizon::ast
