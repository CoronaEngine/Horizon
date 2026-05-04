//
// Created by Z on 22/04/2026.
//

#pragma once

#include "common.h"

namespace ocarina {

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

}// namespace ocarina