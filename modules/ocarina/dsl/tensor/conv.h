//
// Created by Z on 22/04/2026.
//

#pragma once

#include "common.h"

namespace ocarina {

struct ConvOp {
    ConvDesc desc;

    [[nodiscard]] bool valid() const noexcept {
        return desc.valid();
    }
};

[[nodiscard]] inline ConvOp conv2d(ConvDesc desc) noexcept {
    OC_ASSERT(desc.valid());
    return ConvOp{std::move(desc)};
}

}// namespace ocarina