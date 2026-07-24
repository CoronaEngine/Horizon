#version 450

// 移植自 bgfx examples/18-ibl 的 vs_ibl_mesh.sc + vs_ibl_skybox.sc。
// 单 shader 同时承担网格与天空盒两条路径（pc.misc.x 区分）。
//
// 拆分策略：
//   - UBO  vsp：批次共享（proj_view、skyEnvMtx、envMtx、camPos、flags、
//               rgbDiff、rgbSpec、lightDir、lightCol）288 bytes
//   - PC   pc ：per-draw（model、params0、misc）96 bytes
// mvp 在 VS 内由 vsp.proj_view * pc.model 现场计算，不再存储。

layout(binding = 0) uniform IblShared {
    mat4 proj_view;  // proj * view，由 C++ 每帧写入
    mat4 skyEnvMtx;  // 天空盒路径：相机基向量 × 环境旋转
    mat4 envMtx;     // 网格路径：cube 采样方向的环境旋转
    vec4 camPos;     // xyz: 世界空间相机位置
    vec4 flags;      // x: doDiffuse, y: doSpecular, z: doDiffuseIbl, w: doSpecularIbl
    vec4 rgbDiff;
    vec4 rgbSpec;
    vec4 lightDir;
    vec4 lightCol;
} vsp;

layout(push_constant) uniform IblPC {
    mat4 model;    // 网格：per-draw model；天空盒：identity（不使用）
    vec4 params0;  // x: glossiness, y: reflectivity, z: exposure, w: bgType
    vec4 misc;     // x: isSkybox, y: viewport 宽高比(w/h), z: metalOrSpec, w: unused
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 v_view;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec3 v_dir;

void main()
{
    if (pc.misc.x > 0.5)
    {
        // 天空盒：全屏三角形顶点已是 NDC 坐标，钉在远平面 (z=1)。
        gl_Position = vec4(inPosition.xy, 1.0, 1.0);

        float fovHeight = tan(radians(45.0) * 0.5);
        float aspect    = pc.misc.y;
        vec2 tex = inPosition.xy * vec2(fovHeight * aspect, -fovHeight);
        v_dir = (vsp.skyEnvMtx * vec4(tex, 1.0, 0.0)).xyz;

        v_view   = vec3(0.0);
        v_normal = vec3(0.0, 0.0, 1.0);
    }
    else
    {
        mat4 mvp = vsp.proj_view * pc.model;
        gl_Position = mvp * vec4(inPosition, 1.0);
        v_view   = vsp.camPos.xyz - (pc.model * vec4(inPosition, 1.0)).xyz;
        v_normal = (pc.model * vec4(inNormal, 0.0)).xyz;
        v_dir    = vec3(0.0);
    }
}
