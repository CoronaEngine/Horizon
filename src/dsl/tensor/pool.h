//
// Created by Z on 22/04/2026.
//

#pragma once

#include "common.h"

namespace horizon::dsl {
using namespace horizon::core;
using namespace horizon::math;
using namespace horizon::ast;

struct PoolOp {
    PoolDesc desc;

    [[nodiscard]] bool valid() const noexcept {
        return desc.valid();
    }
};

[[nodiscard]] inline PoolOp pool(PoolDesc desc) noexcept {
    OC_ASSERT(desc.valid());
    return PoolOp{std::move(desc)};
}

}// namespace horizon::dsl