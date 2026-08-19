#version 450

// Sponza G-buffer geometry pass, vertex stage.
//
// Unlike deferred_geom_vert.glsl there is no per-draw model matrix: the node
// transforms are already baked into the HZMS vertex data by
// tools/sponza/convert_sponza.py, so object space is world space.
//
// 每-draw 材质不再走 push constant,而是放进 bindless SSBO,由 draw 序号索引
// (见 sponza_geom_frag.glsl 的 PerDrawMaterial)。push constant 因此对整个
// pass 恒定,自动 MDI 合批的 key {pipeline, IB, VB, push constant} 全程一致,
// 数百次 submesh draw 收敛成一条 vkCmdDrawIndexedIndirect。
//
// draw 序号靠 first_instance 携带:instance_count 恒为 1,故
// gl_InstanceIndex == first_instance。相比 gl_DrawID,它在 indirect 与
// 非 indirect 两条路径下语义一致(gl_DrawID 在直接 draw 下恒为 0)。

// set 0-2 are reserved for Horizon bindless tables, so plain UBOs go in set 3.
layout(set = 3, binding = 0) uniform SponzaGeomShared {
    mat4 view_proj;
} vsp;

// Shared with sponza_geom_frag.glsl; the layout must stay identical.
layout(push_constant) uniform SponzaGeomPC {
    uint per_draw_ssbo_index;
    uint vertex_ssbo_index;
} model_pc;

// ---- vertex pulling --------------------------------------------------------
// 顶点不再走顶点属性(此 shader 已无 in 声明,pipeline 的顶点属性表为空),
// 而是用 gl_VertexIndex 从 bindless SSBO 里取。indexed draw 下
// gl_VertexIndex == IB[i] + vertexOffset,正好是全局顶点下标,所以 16-bit
// 重基分段(见 rebase_indices_to_16bit)不影响取值。
//
// HZMS 顶点是 48 字节紧凑布局。std430 里 vec3 会被对齐到 16 字节而把结构撑到
// 64,故必须逐标量声明,保持与 C++ 侧 SponzaVertex 逐字节一致。
struct PulledVertex {
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz, tw; // tw: bitangent sign
    float u, v;
};

layout(set = 1, binding = 0) readonly buffer VertexSSBO {
    PulledVertex verts[];
} vertex_data[];

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_tangent;
layout(location = 2) out vec3 v_bitangent;
layout(location = 3) out vec2 v_texcoord;
// flat int:draw 序号是整数 id,不能被透视插值。
layout(location = 4) flat out int v_draw_index;

void main()
{
    PulledVertex vtx = vertex_data[model_pc.vertex_ssbo_index].verts[uint(gl_VertexIndex)];
    vec3 in_position = vec3(vtx.px, vtx.py, vtx.pz);
    vec3 in_normal = vec3(vtx.nx, vtx.ny, vtx.nz);
    vec4 in_tangent = vec4(vtx.tx, vtx.ty, vtx.tz, vtx.tw);

    gl_Position = vsp.view_proj * vec4(in_position, 1.0);

    // 部分资产的切线与法线并不严格垂直,Gram-Schmidt 重新正交化,
    // 不信任烘焙进来的切线框架。
    vec3 n = normalize(in_normal);
    vec3 t = normalize(in_tangent.xyz - n * dot(n, in_tangent.xyz));

    v_normal = n;
    v_tangent = t;
    v_bitangent = cross(n, t) * in_tangent.w;
    v_texcoord = vec2(vtx.u, vtx.v);
    v_draw_index = gl_InstanceIndex;
}
