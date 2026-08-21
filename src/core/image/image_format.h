//
// Image format metadata shared by Core type descriptions and Math image
// implementations. This header deliberately has no dependency on Math.
//

#pragma once

#include "core/header.h"

namespace horizon::core {

enum struct PixelStorage : uint {
    BYTE1,
    BYTE2,
    BYTE4,
    UINT1,
    UINT2,
    UINT4,
    FLOAT1,
    FLOAT2,
    FLOAT4,
    UNKNOWN
};

}
