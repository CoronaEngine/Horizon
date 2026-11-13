#version 450
#include "raster_batch.slang"
layout(location = 15) flat out uint drawParameterWord;

// 移植自参考示例 16-shadowmaps 的 vs_shadowmaps_color_lighting.sc。
//
// 拆分策略：
//   - UBO  vsp：批次共享（相机矩阵、光源参数、材质，全场景相同）304 bytes
//   - BDA 参数 ：per-draw（model 矩阵，64 bytes）
// mvp / model_view / light_mtx 在 VS 内从 vsp.proj_view * pc.model 等现场计算。

// set 0-2 为 Horizon bindless 保留集，普通 UBO 必须放在 set 3。
layout(set = 3, binding = 0) uniform ShadowSceneShared {
    mat4 proj_view;       // proj * view（CPU 侧预乘）
    mat4 view_matrix;     // view 矩阵（用于法线/视线变换）
    mat4 light_proj_view; // bias * light_proj * light_view（CPU 侧预乘）
    vec4 light_pos_vs;
    vec4 light_ambient;
    vec4 light_diffuse;
    vec4 light_specular;
    vec4 spot_dir_inner_vs;
    vec4 attn_spot_outer;
    vec4 params1;         // x: shadowMapBias, y: normalOffset, z: 1/shadowMapSize
    vec4 material_ka;
    vec4 material_kd;
    vec4 material_ks;     // w: 高光指数
    vec4 color;
} vsp;

// vert/frag 共享同一 BDA 绘制参数，布局必须一致。shadowMapIndex 仅 FS 使用。
struct ShadowScenePC {
    mat4 model; // per-draw
    uint shadowMapIndex;
};
#define model_pc (*(ShadowScenePC*)(drawRoot.parameters + drawParameterWord))

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_view;
layout(location = 2) out vec4 v_shadowcoord;

void main()
{
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + gl_DrawID];
    drawParameterWord = packet.parameterWord;
    uint* vertex = hzPullVertex(packet, gl_VertexIndex);
    vec3 inPosition = *(vec3*)(vertex + 0);
    vec3 inNormal = *(vec3*)(vertex + 3);
    // 从共享矩阵 + per-draw model 现场计算各变换
    mat4 mvp        = vsp.proj_view       * model_pc.model;
    mat4 model_view = vsp.view_matrix     * model_pc.model;
    mat4 light_mtx  = vsp.light_proj_view * model_pc.model;

    gl_Position = mvp * vec4(inPosition, 1.0);

    vec3 normal = inNormal;
    v_normal = normalize((model_view * vec4(normal, 0.0)).xyz);
    v_view   = (model_view * vec4(inPosition, 1.0)).xyz;

    vec3 posOffset = inPosition + normal * vsp.params1.y;
    v_shadowcoord  = light_mtx * vec4(posOffset, 1.0);
}
