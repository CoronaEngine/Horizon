//
// Created by Z on 22/04/2026.
//

#pragma once

#include "tensor_view.h"

namespace ocarina {

enum class PoolType {
    max,
    average
};

struct PoolDesc {
    TensorView input;
    TensorView output;

    PoolType type{PoolType::max};
    uint2 kernel_size{2u, 2u};
    uint2 stride{2u, 2u};
    uint2 padding{0u, 0u};

    [[nodiscard]] bool valid() const noexcept {
        return input.valid() && output.valid();
    }
};

}// namespace ocarina