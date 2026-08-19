#version 450

// Sponza 太阳 RSM(Reflective Shadow Map)pass,顶点阶段。
//
// 在原 depth-only 阴影 pass 的基础上升级为 MRT:除了深度,还输出世界法线与
// flux(albedo × 太阳色),给 light pass 的 RSM 单次弹射间接光 gather 用
// (Dachsbacher & Stamminger 2005;平行光用正交投影,布局同 example_rsm)。
//
// 顶点布局必须与共享的 HZMS 缓冲一致(位置/法线/切线/uv 都在),因为同一份
// 顶点缓冲也喂 G-buffer pass。
//
// 每-draw 材质走 bindless SSBO(由 gl_InstanceIndex 索引),push constant 因此
// 对整个 pass 恒定,数百次 submesh draw 收敛成一条 MDI。详见 sponza_geom_vert.glsl。

layout(set = 3, binding = 0) uniform SponzaShadowShared {
    mat4 light_view_proj;
    vec4 sun_color; // rgb: 太阳色(flux 用), w: 未用
} vsp;

// 与 sponza_shadow_frag.glsl 共享同一 push constant 块,布局必须一致。
layout(push_constant) uniform SponzaShadowPC {
    uint per_draw_ssbo_index;
    uint vertex_ssbo_index;
} model_pc;

// vertex pulling:与 sponza_geom_vert.glsl 同一份顶点 SSBO 与同一套下标语义
// (gl_VertexIndex == IB[i] + vertexOffset)。结构须与 C++ SponzaVertex 逐字节一致,
// std430 下 vec3 会对齐到 16 撑坏步长,故逐标量声明。
struct PulledVertex {
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz, tw;
    float u, v;
};

layout(set = 1, binding = 0) readonly buffer VertexSSBO {
    PulledVertex verts[];
} vertex_data[];

layout(location = 0) out vec2 v_texcoord;
layout(location = 1) out vec3 v_normal;
layout(location = 2) flat out int v_draw_index;

void main()
{
    PulledVertex vtx = vertex_data[model_pc.vertex_ssbo_index].verts[uint(gl_VertexIndex)];

    // 节点变换已烘焙进顶点,object space 即 world space。
    gl_Position = vsp.light_view_proj * vec4(vtx.px, vtx.py, vtx.pz, 1.0);
    v_texcoord = vec2(vtx.u, vtx.v);
    v_normal = vec3(vtx.nx, vtx.ny, vtx.nz);
    v_draw_index = gl_InstanceIndex;
}
