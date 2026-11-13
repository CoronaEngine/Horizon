#version 450
#include "raster_batch.slang"
layout(location = 15) flat in uint drawParameterWord;

// 阴影体示例 pass 3：depth-pass 计数写入 R32F 计数目标（加法混合）。
// 正面 +1 / 背面 -1（对应双面 stencil 的 incr/decr）；被场景遮挡的
// 体片元剔除（采样 R32F 场景深度替代只读深度测试）。

#extension GL_EXT_nonuniform_qualifier : enable

// set 0-2 为 Horizon bindless 保留集，普通 UBO 必须放在 set 3。
layout(set = 3, binding = 0) uniform SvVolumeParams {
    mat4 view_proj;
    vec4 params; // xy: 屏幕分辨率
} fvp;

// bindless combined-image-sampler 表（set 0）+ 与 sv_volume_vert.glsl 共享的 BDA 绘制参数。
layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

// 与 sv_volume_vert.glsl 共享 BDA 绘制参数布局。
struct SvVolumePC {
    uint sceneDepthIndex;
};
#define volume_pc_fs (*(SvVolumePC*)(drawRoot.parameters + drawParameterWord))

#define sceneDepth combinedTextureSamplerHandles[volume_pc_fs.sceneDepthIndex]

layout(location = 0) out vec4 outColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / fvp.params.xy;
    float scene_z = texture(sceneDepth, uv).x;
    if (gl_FragCoord.z > scene_z)
        discard;

    float sign_value = gl_FrontFacing ? 1.0 : -1.0;
    outColor = vec4(sign_value, 0.0, 0.0, 0.0);
}
