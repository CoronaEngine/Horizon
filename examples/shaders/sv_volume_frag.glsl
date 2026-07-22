#version 450

// 阴影体示例 pass 3：depth-pass 计数写入 R32F 计数目标（加法混合）。
// 正面 +1 / 背面 -1（对应双面 stencil 的 incr/decr）；被场景遮挡的
// 体片元剔除（采样 R32F 场景深度替代只读深度测试）。

layout(binding = 0) uniform SvVolumeParams {
    mat4 view_proj;
    vec4 params; // xy: 屏幕分辨率
} fvp;

layout(binding = 2) uniform sampler2D sceneDepth;

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
