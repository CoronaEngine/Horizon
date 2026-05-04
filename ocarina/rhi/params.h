//
// Created by Zero on 2022/8/10.
//

#pragma once

#include "core/stl.h"

namespace ocarina {

enum ShaderTag : uint8_t {
    CS = 1 << 1,
    VS = 1 << 2,
    FS = 1 << 3,
    GS = 1 << 4,
    TS = 1 << 5
};

enum AccelUsageTag : uint8_t {
    FAST_BUILD,
    FAST_UPDATE,
    FAST_TRACE
};

enum AccelBuildTag : uint8_t {
    BUILD,
    UPDATE
};

enum AccelGeomTag : uint8_t {
    NONE = 0,
    DISABLE_ANYHIT = 1 << 0,
    SINGLE_ANYHIT_CALL = 1 << 1,
    DISABLE_FACE_CULLING = 1 << 2
};

struct MeshParams {
    handle_ty vert_handle{};
    uint vert_offset{};
    uint vert_stride{};
    uint vert_num{};

    handle_ty tri_handle{};
    uint tri_offset{};
    uint tri_stride{};
    uint tri_num{};

    AccelUsageTag usage_tag{FAST_TRACE};
    AccelGeomTag geom_tag{DISABLE_ANYHIT};
};

}// namespace ocarina