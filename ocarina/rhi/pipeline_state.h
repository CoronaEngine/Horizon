//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "graphics_descriptions.h"

namespace ocarina {
template<typename T>
class Shader;
class VertexBuffer;

struct RasterState {
    CullingMode cull_mode : 2;
    bool front_face : 1;
    bool depth_bias : 1;
    bool depth_clamp : 1;
    int padding : 27;

    static RasterState Default()
    {
        RasterState state;
        state.cull_mode = CullingMode::BACK;
        state.front_face = false;// Counter-clockwise
        state.depth_bias = false;
        state.depth_clamp = false;
        return state;
    }
};

struct BlendState {
    BlendFunction srccolorblend_factor : 5 = BlendFunction::ONE;
    BlendFunction dstcolorblend_factor : 5 = BlendFunction::ZERO;
    BlendFunction srcalphablend_factor : 5 = BlendFunction::ONE;
    BlendFunction dstalphablend_factor : 5 = BlendFunction::ZERO;
    BlendOperator colorBlendOp : 3 = BlendOperator::ADD;
    BlendOperator alphaBlendOp : 3 = BlendOperator::ADD;
    ColorMask color_mask : 4 = ColorMask::ColorMaskAll;///< Color mask for the blend state.
    bool blend_enable : 1 = false;
    int padding : 1;

    static BlendState Opaque()
    {
        BlendState state;
        state.srccolorblend_factor = BlendFunction::ONE;
        state.dstcolorblend_factor = BlendFunction::ZERO;
        state.srcalphablend_factor = BlendFunction::ONE;
        state.dstalphablend_factor = BlendFunction::ZERO;
        state.colorBlendOp = BlendOperator::ADD;
        state.alphaBlendOp = BlendOperator::ADD;
        state.blend_enable = false;
        return state;
    }

    static BlendState AlphaBlend() {
        BlendState state;
        state.srccolorblend_factor = BlendFunction::SRC_ALPHA;
        state.dstcolorblend_factor = BlendFunction::ONE_MINUS_SRC_ALPHA;
        state.srcalphablend_factor = BlendFunction::SRC_ALPHA;
        state.dstalphablend_factor = BlendFunction::ONE_MINUS_SRC_ALPHA;
        state.colorBlendOp = BlendOperator::ADD;
        state.alphaBlendOp = BlendOperator::ADD;
        state.blend_enable = true;
        return state;
    }
};

struct DepthStencilState {
    bool depth_test_enable : 1;
    bool depth_write_enable : 1;
    SamplerCompareFunc depth_compare_op : 4;
    bool depth_bounds_test_enable : 1;
    bool stencil_test_enable : 1;
    int32_t padding : 25;

    static DepthStencilState Default() {
        DepthStencilState state;
        state.depth_test_enable = true;
        state.depth_write_enable = true;
        state.depth_compare_op = SamplerCompareFunc::L;
        state.depth_bounds_test_enable = false;
        state.stencil_test_enable = false;
        return state;
    }
};

struct MultiSampleState {
    MultiSampleCount sample_count : 3 = MultiSampleCount::SAMPLE_COUNT_1;///< Number of samples per pixel.
    bool alpha_to_coverage_enable : 1 = false; ///< Enable alpha to coverage.
    bool alpha_to_one_enable : 1 = false;      ///< Enable alpha to one.
    bool sample_shading_enable : 1 = false;
    bool shading_rate_image_enable : 1 = false;///< Enable shading rate image.
    uint32_t padding : 25 = 0;
    //uint32_t sample_mask : 32 = 0xFFFFFFFF;     ///< Sample mask for multisampling.
    
};

struct PipelineState {
    static constexpr uint16_t MAX_SHADER_STAGE = 3;
    handle_ty shaders[MAX_SHADER_STAGE];
    handle_ty descriptorset_layout = InvalidUI64;        
    VertexBuffer *vertex_buffer = nullptr;             
    RasterState raster_state;//  4
    BlendState blend_state;
    DepthStencilState depth_stencil_state;  
    MultiSampleState multiple_sample_state;
    PrimitiveType primitive_type = PrimitiveType::TRIANGLES;

    

    bool operator!=(const PipelineState &other) const {
        return shaders[0] != other.shaders[0] ||
            shaders[1] != other.shaders[1] ||
            shaders[2] != other.shaders[2] ||
            descriptorset_layout != other.descriptorset_layout ||
            vertex_buffer != other.vertex_buffer ||
            raster_state.cull_mode != other.raster_state.cull_mode ||
            raster_state.front_face != other.raster_state.front_face ||
            raster_state.depth_bias != other.raster_state.depth_bias ||
            raster_state.depth_clamp != other.raster_state.depth_clamp ||
            blend_state.srccolorblend_factor != other.blend_state.srccolorblend_factor ||
            blend_state.dstcolorblend_factor != other.blend_state.dstcolorblend_factor ||
            blend_state.srcalphablend_factor != other.blend_state.srcalphablend_factor ||
            blend_state.dstalphablend_factor != other.blend_state.dstalphablend_factor ||
            blend_state.colorBlendOp != other.blend_state.colorBlendOp ||
            blend_state.alphaBlendOp != other.blend_state.alphaBlendOp ||
            blend_state.color_mask != other.blend_state.color_mask ||
            blend_state.blend_enable != other.blend_state.blend_enable ||
            depth_stencil_state.depth_test_enable != other.depth_stencil_state.depth_test_enable ||
            depth_stencil_state.depth_write_enable != other.depth_stencil_state.depth_write_enable ||
            depth_stencil_state.depth_compare_op != other.depth_stencil_state.depth_compare_op ||
            depth_stencil_state.depth_bounds_test_enable != other.depth_stencil_state.depth_bounds_test_enable ||
            depth_stencil_state.stencil_test_enable != other.depth_stencil_state.stencil_test_enable ||
            multiple_sample_state.sample_count != other.multiple_sample_state.sample_count ||
            multiple_sample_state.alpha_to_coverage_enable != other.multiple_sample_state.alpha_to_coverage_enable ||
            multiple_sample_state.alpha_to_one_enable != other.multiple_sample_state.alpha_to_one_enable ||
            multiple_sample_state.sample_shading_enable != other.multiple_sample_state.sample_shading_enable;

    }

    bool operator==(const PipelineState &other) const {
        return !(*this != other);
    }
};

struct RHIPipeline
{

};

}// namespace ocarina