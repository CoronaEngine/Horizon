#pragma once

#include "../api/stmt_builder.h"
#include "core/type_system/type_desc.h"

namespace ocarina {

// Workgroup storage is uninitialized. All participating threads must write the
// entries they will read, then call synchronize_block() before reading them.
template<typename T, size_t N>
class SharedArray {
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, float2> ||
                  std::is_same_v<T, float3> || std::is_same_v<T, float4> ||
                  std::is_same_v<T, uint> || std::is_same_v<T, uint2> ||
                  std::is_same_v<T, uint3> || std::is_same_v<T, uint4> ||
                  std::is_same_v<T, int> || std::is_same_v<T, int2> ||
                  std::is_same_v<T, int3> || std::is_same_v<T, int4>,
                  "SharedArray requires a 32-bit scalar or vector");
    static_assert(N > 0u);
    const RefExpr *expression_;

    [[nodiscard]] const SubscriptExpr *element(const Uint &index) const noexcept {
        OC_ERROR_IF_NOT(Function::current() == expression_->context(),
                        "Shared arrays must be accessed in their defining kernel");
        return Function::current()->subscript(Type::of<T>(), expression_, index.expression());
    }

public:
    explicit SharedArray(std::source_location location = std::source_location::current()) noexcept
        : expression_(Function::current()->shared(
              Type::from(ocarina::format("array<{},{}>", TypeDesc<T>::description(), N)), location)) {}
    SharedArray(const SharedArray &) = delete;
    SharedArray &operator=(const SharedArray &) = delete;

    [[nodiscard]] Var<T> read(const Uint &index) const noexcept {
        return eval<T>(element(index));
    }

    void write(const Uint &index, const Var<T> &value) const noexcept {
        Function::current()->assign(element(index), value.expression());
    }
};

}// namespace ocarina
