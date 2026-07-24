#version 450

// 移植自参考示例 21-deferred 的 vs_deferred_light.sc。
//
// 拆分策略：
//   - UBO  vsp：inv_mvp + view（批次共享，所有光源相同的相机矩阵）128 bytes
//   - PC   pc ：per-draw（light_pos_radius/light_rgb_inner_r/rect，每光不同）48 bytes

layout(binding = 0) uniform DeferredLightShared {
    mat4 inv_mvp;
    mat4 view;
} vsp;

layout(push_constant) uniform DeferredLightPC {
    vec4 light_pos_radius;
    vec4 light_rgb_inner_r;
    vec4 rect; // xy: NDC min, zw: NDC max
} pc;

layout(location = 0) in vec3 inCorner;

layout(location = 0) out vec2 v_texcoord;

void main()
{
    vec2 ndc = mix(pc.rect.xy, pc.rect.zw, inCorner.xy);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_texcoord = ndc * 0.5 + 0.5;
}
