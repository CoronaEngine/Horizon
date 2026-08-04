#version 450

// 39-assao G-buffer 深度 pass：器件深度写 R32F（供 compute 重建 view 空间坐标）。

layout(binding = 0) uniform AssaoSceneShared {
    mat4 proj_view;
    mat4 view_matrix;
} fsp;

layout(push_constant) uniform AssaoScenePC {
    mat4 model;
    vec4 color;
} fpc;

layout(location = 0) in vec3 v_normal_vs;

layout(location = 0) out vec4 outDepthVal;

void main()
{
    outDepthVal = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
