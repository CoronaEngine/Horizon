#version 450

// VP：全局 uniform buffer。set 0-2 为 Horizon bindless 保留集（0=纹理/1=storage buffer/
// 2=storage image），普通 UBO 必须放在 set 3（与 EDSL bindless 约定一致）。
layout(set = 3, binding = 0) uniform ViewProj {
    mat4 view;
    mat4 proj;
} vp;

// push constant：per-draw 的 model 矩阵 + bindless 纹理索引（texIndex）。
// vert/frag 共享同一 push constant 块，布局必须一致。
layout(push_constant) uniform PushConsts {
    mat4 model;
    uint texIndex;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    gl_Position = vp.proj * vp.view * pc.model * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
