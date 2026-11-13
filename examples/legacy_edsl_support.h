#pragma once

#include "legacy_common.h"
#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"
#include <cstring>

namespace legacy_example::edsl {
using namespace EmbeddedShader;
using namespace ktm;

inline fmat4x4 value(const glm::mat4& m) {
    static_assert(sizeof(fmat4x4) == sizeof(m));
    fmat4x4 result;
    std::memcpy(&result, &m, sizeof(result));
    return result;
}
inline fvec4 value(const glm::vec4& v) { return {v.x, v.y, v.z, v.w}; }

inline EmbeddedShader::CompilerOption options() {
    EmbeddedShader::CompilerOption result;
    result.enableMatrixColumnMajor = true;
    return result;
}
} // namespace legacy_example::edsl
