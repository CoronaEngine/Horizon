//
// Created by Z on 22/04/2026.
//

#pragma once

#include "gemm.h"
#include "dsl/api/stmt_builder.h"
#include "rhi/device.h"
#include "rhi/resources/shader.h"

namespace ocarina {

namespace detail {

[[nodiscard]] inline bool _gemm_is_contiguous_row_major(const TensorView &view,
                                                        uint rows,
                                                        uint cols) noexcept {
    if (view.layout != TensorLayout::row_major || view.rank() != 2u) {
        return false;
    }
    if (view.shape[0] != rows || view.shape[1] != cols) {
        return false;
    }
    if (view.stride.empty()) {
        return true;
    }
    return view.stride.size() == 2u &&
           view.stride[0] == cols &&
           view.stride[1] == 1u;
}

[[nodiscard]] inline BufferView<float> _gemm_make_buffer_view(const TensorView &view,
                                                              uint rows,
                                                              uint cols) noexcept {
    OC_ASSERT(view.byte_offset % sizeof(float) == 0u);
    size_t element_offset = view.byte_offset / sizeof(float);
    size_t element_count = static_cast<size_t>(rows) * static_cast<size_t>(cols);
    return BufferView<float>(view.buffer,
                             element_offset,
                             element_count,
                             element_offset + element_count);
}

[[nodiscard]] inline bool _supports_naive_float_gemm(const GemmDesc &desc) noexcept {
    if (!desc.valid()) {
        return false;
    }
    if (desc.a.element_type != Type::of<float>() ||
        desc.b.element_type != Type::of<float>() ||
        desc.c.element_type != Type::of<float>()) {
        return false;
    }
    return _gemm_is_contiguous_row_major(desc.a,
                                         desc.trans_a ? desc.k : desc.m,
                                         desc.trans_a ? desc.m : desc.k) &&
           _gemm_is_contiguous_row_major(desc.b,
                                         desc.trans_b ? desc.n : desc.k,
                                         desc.trans_b ? desc.k : desc.n) &&
           _gemm_is_contiguous_row_major(desc.c, desc.m, desc.n);
}

}// namespace detail

[[nodiscard]] inline CommandBatch gemm_commands(Device &device,
                                                const GemmDesc &desc,
                                                string shader_desc = "tensor_gemm_naive") noexcept {
    OC_ASSERT(detail::_supports_naive_float_gemm(desc));

    BufferView<float> a = detail::_gemm_make_buffer_view(desc.a,
                                                         desc.trans_a ? desc.k : desc.m,
                                                         desc.trans_a ? desc.m : desc.k);
    BufferView<float> b = detail::_gemm_make_buffer_view(desc.b,
                                                         desc.trans_b ? desc.n : desc.k,
                                                         desc.trans_b ? desc.k : desc.n);
    BufferView<float> c = detail::_gemm_make_buffer_view(desc.c, desc.m, desc.n);

    Kernel kernel = [&](BufferVar<float> lhs,
                        BufferVar<float> rhs,
                        BufferVar<float> out,
                        Uint m,
                        Uint n,
                        Uint k,
                        Bool trans_a,
                        Bool trans_b,
                        Float alpha,
                        Float beta) {
        Uint linear = dispatch_id();
        $if(linear < m * n) {
            Uint row = linear / n;
            Uint col = linear % n;
            Float accum = 0.f;
            $for(kk, k) {
                Uint lhs_index = select(trans_a,
                                        kk * m + row,
                                        row * k + kk);
                Uint rhs_index = select(trans_b,
                                        col * k + kk,
                                        kk * n + col);
                accum += lhs.read(lhs_index) * rhs.read(rhs_index);
            };
            Float prev = out.read(linear);
            out.write(linear, alpha * accum + beta * prev);
        };
    };

    auto compiled = device.compile(kernel, shader_desc);
    auto shader = make_shared<decltype(compiled)>(std::move(compiled));

    CommandBatch commands;
    commands << (*shader)(Buffer<float>(a),
                          Buffer<float>(b),
                          Buffer<float>(c),
                          desc.m,
                          desc.n,
                          desc.k,
                          desc.trans_a,
                          desc.trans_b,
                          desc.alpha,
                          desc.beta)
                    .dispatch(desc.m * desc.n);
    commands << keep_alive(true, shader);
    return commands;
}

}// namespace ocarina