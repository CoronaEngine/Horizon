//
// Created by Z on 22/04/2026.
//

#pragma once

#include "core/type.h"

namespace ocarina {

enum class TensorLayout {
    row_major,
    col_major
};

enum class GemmComputeType {
    fp16_accum_fp16,
    fp16_accum_fp32,
    fp32
};

}// namespace ocarina