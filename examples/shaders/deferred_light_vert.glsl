#version 450

// 移植自参考示例 21-deferred 的 vs_deferred_light.sc。
// 原版 用 scissor 限制光照 quad 的着色范围；Horizon 侧等价做法：
// 单位角点 quad 按 rect（光源包围盒的 NDC 范围）在 VS 里直接定位。
// VS/FS 共用同一 layout 的 uniform block，实例名 vsp/fsp。

layout(binding = 0) uniform DeferredLightParams {
    mat4 inv_mvp;
    mat4 view;
    vec4 light_pos_radius;
    vec4 light_rgb_inner_r;
    vec4 rect; // xy: NDC min, zw: NDC max
} vsp;

layout(location = 0) in vec3 inCorner; // (0,0)..(1,1)

layout(location = 0) out vec2 v_texcoord;

void main()
{
    vec2 ndc = mix(vsp.rect.xy, vsp.rect.zw, inCorner.xy);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_texcoord = ndc * 0.5 + 0.5; // Vulkan：NDC y 向下与纹理 v 向下一致
}
