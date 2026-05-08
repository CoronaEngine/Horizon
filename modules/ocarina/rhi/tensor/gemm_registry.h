//
// Created by Z on 22/04/2026.
//

#pragma once

#include "gemm_desc.h"

namespace ocarina {

[[nodiscard]] uint register_gemm_desc(GemmDesc desc) noexcept;
[[nodiscard]] const GemmDesc *find_gemm_desc(uint id) noexcept;

}// namespace ocarina