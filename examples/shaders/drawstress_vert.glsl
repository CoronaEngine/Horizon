#version 450

// 完全 vertex pulling:per-draw MVP 与顶点数据都从 bindless SSBO 取。
// 没有任何 in 声明,pipeline 的顶点属性表为空;push constant 对整个 pass 恒定,
// 于是自动 MDI 合批的 key {pipeline, IB, VB, push constant} 全程一致,
// 数万个 cube draw 可以塌成一条 vkCmdDrawIndexedIndirect。
//
// Direct draw 下用 gl_InstanceIndex (由 first_instance 携带 draw index) 而非
// gl_DrawID (separate vkCmdDrawIndexed 时恒为 0)。MDI 下 gl_DrawID 可用但
// 此处统一用 gl_InstanceIndex 以兼容两种路径。

layout(push_constant) uniform DrawStressPC {
    uint per_draw_ssbo_index;
    uint vertex_ssbo_index;
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
    PerDrawData draw = per_draw_data[model_pc.per_draw_ssbo_index].draws[uint(gl_InstanceIndex)];

    gl_Position = draw.mvp * vec4(vtx.px, vtx.py, vtx.pz, 1.0);
    v_color = vec3(vtx.cr, vtx.cg, vtx.cb);
}
