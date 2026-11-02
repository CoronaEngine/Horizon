//
// Created by Z on 12/04/2026.
//

#pragma once

#include "core/stl.h"

namespace ocarina {

enum class PrecisionPolicy : uint8_t {
    force_f32,
    force_f16,
};

struct DevicePrecisionCaps {
    uint32_t compute_capability{0};
    size_t total_vram_bytes{0};
    bool has_native_fp16{false};
    bool has_fast_fp16{false};
    bool has_tensor_fp16{false};

    [[nodiscard]] PrecisionPolicy recommend_policy(
        size_t estimated_scene_vram_bytes = 0) const noexcept;
};

struct StoragePrecisionPolicy {
    PrecisionPolicy policy = PrecisionPolicy::force_f32;
    bool allow_real_in_storage = false;
};

void set_global_storage_policy(StoragePrecisionPolicy policy) noexcept;
[[nodiscard]] StoragePrecisionPolicy global_storage_policy() noexcept;

}// namespace ocarina