// Legacy direct draws: explicit draw_index replaces indirect first_instance.
#version 450

// 与新版相同的 vertex pulling 和 per-draw MVP 表。
// Legacy 逐次直接绘制，通过 push constant 指定 draw_index。

layout(push_constant) uniform DrawStressPC {
    uint per_draw_ssbo_index;
    uint vertex_ssbo_index;
    uint draw_index;
} model_pc;

struct PerDrawData {
    mat4 mvp;
};

layout(set = 1, binding = 0) readonly buffer PerDrawSSBO {
    PerDrawData draws[];
} per_draw_data[];

// StressVertex 是 24 字节紧凑布局(3 float 位置 + 3 float 颜色)。std430 会把 vec3
// 对齐到 16 字节、把结构撑到 32,故必须逐标量声明才能与 C++ 侧逐字节一致。
struct PulledVertex {
    float px, py, pz;
    float cr, cg, cb;
};

layout(set = 1, binding = 0) readonly buffer VertexSSBO {
    PulledVertex verts[];
} vertex_data[];

layout(location = 0) out vec3 v_color;

void main()
{
    PulledVertex vtx = vertex_data[model_pc.vertex_ssbo_index].verts[uint(gl_VertexIndex)];
    PerDrawData draw = per_draw_data[model_pc.per_draw_ssbo_index].draws[model_pc.draw_index];

    gl_Position = draw.mvp * vec4(vtx.px, vtx.py, vtx.pz, 1.0);
    v_color = vec3(vtx.cr, vtx.cg, vtx.cb);
}
