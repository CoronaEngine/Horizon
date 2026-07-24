#version 450

// VP：全局 uniform buffer，每帧相机固定时只需绑定一次
layout(set = 0, binding = 0) uniform ViewProj {
    mat4 view;
    mat4 proj;
} vp;

// Model：push constant，每个 draw call 独立更新（64 bytes，符合 Vulkan 128 bytes 保证）
layout(push_constant) uniform Model {
    mat4 model;
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
