#version 450

// 阴影体示例 pass 3：CPU 生成的世界空间阴影体（轮廓边挤出），只需 view_proj。

layout(binding = 0) uniform SvVolumeParams {
    mat4 view_proj;
    vec4 params; // xy: 屏幕分辨率
} vvp;

layout(location = 0) in vec3 inPosition;

void main()
{
    gl_Position = vvp.view_proj * vec4(inPosition, 1.0);
}
