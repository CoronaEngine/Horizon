//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "graphics_descriptions.h"

namespace ocarina {

struct FrameData
{
    uint32_t frame_index{0};// Current frame index
    float4x4 view_matrix{1.0f};// View matrix for the current frame
    float4x4 projection_matrix{1.0f};// Projection matrix for the current frame
    float4x4 inv_view_matrix{1.0f};  // Inverse view matrix for the current frame
    float4x4 inv_projection_matrix{1.0f};// Inverse projection matrix for the current frame
};

}// namespace ocarina