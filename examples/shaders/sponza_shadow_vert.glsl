#version 450
#include "raster_batch.slang"
layout(location = 15) flat out uint drawParameterWord;

// Sponza 太阳 RSM(Reflective Shadow Map)pass,顶点阶段。
//
// 在原 depth-only 阴影 pass 的基础上升级为 MRT:除了深度,还输出世界法线与
// flux(albedo × 太阳色),给 light pass 的 RSM 单次弹射间接光 gather 用
// (Dachsbacher & Stamminger 2005;平行光用正交投影,布局同 example_rsm)。
//
// 顶点布局必须与共享的 HZMS 缓冲一致(位置/法线/切线/uv 都在),因为同一份
// 顶点缓冲也喂 G-buffer pass。
//
// firstInstance 选中子网格在材质 SSBO 中的条目。

layout(set = 3, binding = 0) uniform SponzaShadowShared {
    mat4 light_view_proj;
    vec4 sun_color; // rgb: 太阳色(flux 用), w: 未用
} vsp;

// 与 sponza_shadow_frag.glsl 共享同一 BDA 绘制参数,布局必须一致。
struct SponzaShadowPC {
    uint per_draw_ssbo_index;
};
#define model_pc (*(SponzaShadowPC*)(drawRoot.parameters + drawParameterWord))

// BDA 读取紧凑的 SponzaVertex。逐标量字段确保 48 字节步长。
struct PulledVertex {
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz, tw;
    float u, v;
};

layout(location = 0) out vec2 v_texcoord;
layout(location = 1) out vec3 v_normal;
layout(location = 2) flat out int v_draw_index;

void main()
{
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + gl_DrawID];
    drawParameterWord = packet.parameterWord;
    uint* vertex = hzPullVertex(packet, gl_VertexIndex);
    PulledVertex vtx = *(PulledVertex*)vertex;

    // 节点变换已烘焙进顶点,object space 即 world space。
    gl_Position = vsp.light_view_proj * vec4(vtx.px, vtx.py, vtx.pz, 1.0);
    v_texcoord = vec2(vtx.u, vtx.v);
    v_normal = vec3(vtx.nx, vtx.ny, vtx.nz);
    v_draw_index = gl_InstanceIndex;
}
