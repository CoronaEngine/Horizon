#version 450

// Sponza G-buffer geometry pass, vertex stage.
//
// Unlike deferred_geom_vert.glsl there is no per-draw model matrix: the node
// transforms are already baked into the HZMS vertex data by
// tools/sponza/convert_sponza.py, so object space is world space.
//
// Split: UBO vsp holds the camera (shared by every submesh); the push constant
// holds the per-submesh material, which changes on every draw.

// set 0-2 are reserved for Horizon bindless tables, so plain UBOs go in set 3.
layout(set = 3, binding = 0) uniform SponzaGeomShared {
    mat4 view_proj;
} vsp;

// Shared with sponza_geom_frag.glsl; the layout must stay identical.
layout(push_constant) uniform SponzaGeomPC {
    vec4 base_color_factor;
    vec4 mr_factor; // x: metallicFactor, y: roughnessFactor, zw: unused
    uint tex_base_color_index;
    uint tex_normal_index;
    uint tex_metal_rough_index;
    uint tex_mask_index;
    uint material_flags; // bit0: normal map, bit1: alpha mask, bit2: MR map, bit3: mask 采 alpha
} model_pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent; // w: bitangent sign
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_tangent;
layout(location = 2) out vec3 v_bitangent;
layout(location = 3) out vec2 v_texcoord;

void main()
{
    gl_Position = vsp.view_proj * vec4(inPosition, 1.0);

    // 部分资产的切线与法线并不严格垂直,Gram-Schmidt 重新正交化,
    // 不信任烘焙进来的切线框架。
    vec3 n = normalize(inNormal);
    vec3 t = normalize(inTangent.xyz - n * dot(n, inTangent.xyz));

    v_normal = n;
    v_tangent = t;
    v_bitangent = cross(n, t) * inTangent.w;
    v_texcoord = inTexCoord;
}
