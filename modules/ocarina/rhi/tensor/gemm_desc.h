//
// Created by Z on 22/04/2026.
//

#pragma once

#include "tensor_view.h"

namespace ocarina {

struct GemmDesc {
    TensorView a;
    TensorView b;
    TensorView c;

    uint m{};
    uint n{};
    uint k{};

    bool trans_a{false};
    bool trans_b{false};

    float alpha{1.f};
    float beta{0.f};

    GemmComputeType compute_type{GemmComputeType::fp16_accum_fp32};
    bool allow_tensor_core{true};

    [[nodiscard]] bool valid() const noexcept {
        return a.valid() && b.valid() && c.valid() && m > 0u && n > 0u && k > 0u;
    }
};

}// namespace ocarina