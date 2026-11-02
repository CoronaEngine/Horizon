//
// Created by Z on 22/04/2026.
//

#pragma once

#include "tensor_view.h"

namespace ocarina {

struct ConvDesc {
    TensorView input;
    TensorView weight;
    TensorView output;
    TensorView bias;

    uint2 stride{1u, 1u};
    uint2 padding{0u, 0u};
    uint2 dilation{1u, 1u};
    bool has_bias{false};

    [[nodiscard]] bool valid() const noexcept {
        return input.valid() && weight.valid() && output.valid();
    }
};

}// namespace ocarina