#version 450
#extension GL_EXT_nonuniform_qualifier : enable

// Horizon bindless combined-image-sampler 表（set 0）。纹理经 image.store_descriptor()
// 存入该表后返回索引，通过 push constant 传入 shader 索引采样。
layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

// 与 baseline_vert.glsl 共享同一 push constant 块（布局一致）。frag 仅用 texIndex。
// EDSL 同时继承 vert/frag 的 ResourceBindings，两 stage 实例名必须不同以避免 C++ 端
// 成员访问二义（C2385）。C++ 通过 vert 的 pc 写入，frag 用 pc_fs 只读。
layout(push_constant) uniform PushConsts {
    mat4 model;
    uint texIndex;
} pc_fs;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(combinedTextureSamplerHandles[pc_fs.texIndex], fragTexCoord);
}
