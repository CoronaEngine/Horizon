//
// Image format metadata shared by Core type descriptions and Math image
// implementations. This header deliberately has no dependency on Math.
//

#pragma once

#include "core/header.h"

namespace horizon::core {

enum struct PixelStorage : uint {
    Byte1,
    Byte2,
    Byte4,
    Uint1,
    Uint2,
    Uint4,
    Float1,
    Float2,
    Float4,
    Unknown
};

}
