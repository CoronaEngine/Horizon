//
// Created by Z on 22/04/2026.
//

#pragma once

#include "common.h"

namespace horizon::dsl {
using namespace horizon::core;
using namespace horizon::math;
using namespace horizon::ast;

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

}// namespace horizon::dsl