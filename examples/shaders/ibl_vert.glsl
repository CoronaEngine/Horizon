#version 450

// 移植自 bgfx examples/18-ibl 的 vs_ibl_mesh.sc + vs_ibl_skybox.sc。
// 单 shader 同时承担网格与天空盒两条路径（misc.x 区分），
// 这样两类 draw 可以共用一个 RasterizerPipeline / 一个 render pass。
//
// 注意：Horizon 的 BoundField 按 (set=0, binding=0) 寻址 uniform 成员，
// 因此 VS/FS 共用同一个 binding=0 的 uniform block（成员布局必须完全一致，
// 实例名错开为 vsp/fsp 以避免生成的 C++ 绑定成员重名歧义）。

layout(binding = 0) uniform IblParams {
    mat4 mvp;       // proj * view * model（网格路径）
    mat4 model;     // model 矩阵（网格路径）
    mat4 skyEnvMtx; // 天空盒路径：相机基向量 × 环境旋转
    mat4 envMtx;    // 网格路径：cube 采样方向的环境旋转
    vec4 camPos;    // xyz: 世界空间相机位置
    vec4 misc;      // x: isSkybox, y: viewport 宽高比 (w/h), z: metalOrSpec
    vec4 params0;   // x: glossiness, y: reflectivity, z: exposure, w: bgType
    vec4 flags;     // x: doDiffuse, y: doSpecular, z: doDiffuseIbl, w: doSpecularIbl
    vec4 rgbDiff;
    vec4 rgbSpec;
    vec4 lightDir;
    vec4 lightCol;
} vsp;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 v_view;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec3 v_dir;

void main()
{
    if (vsp.misc.x > 0.5)
    {
        // 天空盒：全屏三角形顶点已是 NDC 坐标，钉在远平面 (z=1)。
        gl_Position = vec4(inPosition.xy, 1.0, 1.0);

        float fovHeight = tan(radians(45.0) * 0.5);
        float aspect = vsp.misc.y;
        // Vulkan NDC 的 y 朝下，翻转以得到世界空间 y 朝上的视线方向。
        vec2 tex = inPosition.xy * vec2(fovHeight * aspect, -fovHeight);
        v_dir = (vsp.skyEnvMtx * vec4(tex, 1.0, 0.0)).xyz;

        v_view = vec3(0.0);
        v_normal = vec3(0.0, 0.0, 1.0);
    }
    else
    {
        gl_Position = vsp.mvp * vec4(inPosition, 1.0);
        v_view = vsp.camPos.xyz - (vsp.model * vec4(inPosition, 1.0)).xyz;
        v_normal = (vsp.model * vec4(inNormal, 0.0)).xyz;
        v_dir = vec3(0.0);
    }
}
