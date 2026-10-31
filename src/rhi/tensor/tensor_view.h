//
// Created by Z on 22/04/2026.
//

#pragma once

#include "common.h"
#include "rhi/resources/buffer.h"

namespace ocarina {

struct TensorView {
    handle_ty buffer{};
    size_t byte_offset{};
    const Type *element_type{};
    vector<uint> shape;
    vector<uint> stride;
    TensorLayout layout{TensorLayout::row_major};

    [[nodiscard]] bool valid() const noexcept {
        return buffer != 0u && element_type != nullptr && !shape.empty();
    }

    [[nodiscard]] uint rank() const noexcept {
        return static_cast<uint>(shape.size());
    }
};

template<typename T>
[[nodiscard]] inline TensorView make_tensor_view(const Buffer<T> &buffer,
                                                 vector<uint> shape,
                                                 vector<uint> stride = {},
                                                 TensorLayout layout = TensorLayout::row_major,
                                                 size_t byte_offset = 0u) noexcept {
    if (stride.empty() && !shape.empty()) {
        stride.resize(shape.size(), 1u);
        for (size_t index = shape.size(); index > 1u; --index) {
            stride[index - 2u] = stride[index - 1u] * shape[index - 1u];
        }
    }
    return TensorView{buffer.handle(), byte_offset, Type::of<T>(), std::move(shape), std::move(stride), layout};
}

}// namespace ocarina