#version 450

// 阴影体示例 pass 3：depth-pass 计数写入 R32F 计数目标（加法混合）。
// 正面 +1 / 背面 -1（对应双面 stencil 的 incr/decr）；被场景遮挡的
// 体片元剔除（采样 R32F 场景深度替代只读深度测试）。

#extension GL_EXT_nonuniform_qualifier : enable

// set 0-2 为 Horizon bindless 保留集，普通 UBO 必须放在 set 3。
layout(set = 3, binding = 0) uniform SvVolumeParams {
    mat4 view_proj;
    vec4 params; // xy: 屏幕分辨率
} fvp;

// bindless combined-image-sampler 表（set 0）+ 与 sv_volume_vert.glsl 共享的 push constant。
layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

// 与 sv_volume_vert.glsl 共享同一 push constant 块（布局一致）。EDSL 同时继承 vert/frag
// 的 ResourceBindings，两 stage 实例名必须不同以避免 C++ 端二义（C2385）。
// C++ 通过 vert 的 volume_pc 写入，frag 用 volume_pc_fs 只读。
layout(push_constant) uniform SvVolumePC {
    uint sceneDepthIndex;
} volume_pc_fs;

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
