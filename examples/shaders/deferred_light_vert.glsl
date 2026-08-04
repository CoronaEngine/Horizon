#version 450

// 移植自参考示例 21-deferred 的 vs_deferred_light.sc。
//
// 拆分策略：
//   - UBO  vsp：inv_mvp + view（批次共享，所有光源相同的相机矩阵）128 bytes
//   - PC   pc ：per-draw（light_pos_radius/light_rgb_inner_r/rect，每光不同）48 bytes

// set 0-2 为 Horizon bindless 保留集，普通 UBO 必须放在 set 3。
layout(set = 3, binding = 0) uniform DeferredLightShared {
    mat4 inv_mvp;
    mat4 view;
} vsp;

// vert/frag 共享同一 push constant 块，布局必须一致。gNormalIndex/gDepthIndex 仅 FS 使用。
layout(push_constant) uniform DeferredLightPC {
    vec4 light_pos_radius;
    vec4 light_rgb_inner_r;
    vec4 rect; // xy: NDC min, zw: NDC max
    uint gNormalIndex;
    uint gDepthIndex;
} vpc;

layout(location = 0) in vec3 inCorner;

layout(location = 0) out vec2 v_texcoord;

void main()
{
    vec2 ndc = mix(vpc.rect.xy, vpc.rect.zw, inCorner.xy);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_texcoord = ndc * 0.5 + 0.5;
}
