#version 450

// 阴影体示例 pass 3：CPU 生成的世界空间阴影体（轮廓边挤出），只需 view_proj。

// set 0-2 为 Horizon bindless 保留集，普通 UBO 必须放在 set 3。
layout(set = 3, binding = 0) uniform SvVolumeParams {
    mat4 view_proj;
    vec4 params; // xy: 屏幕分辨率
} vvp;

// vert/frag 共享同一 push constant 块。sceneDepthIndex 仅 FS 使用。
layout(push_constant) uniform SvVolumePC {
    uint sceneDepthIndex;
} volume_pc;

layout(location = 0) in vec3 inPosition;

void main()
{
    gl_Position = vvp.view_proj * vec4(inPosition, 1.0);
}
