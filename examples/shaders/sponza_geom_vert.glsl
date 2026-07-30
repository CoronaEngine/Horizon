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
    vec4 spec_range;   // x: min specular, y: max specular, z: min gloss, w: max gloss
    vec4 no_map_material; // x: specular, y: gloss (materials without a '*_spec' map)
} vsp;

// Shared with sponza_geom_frag.glsl; the layout must stay identical.
// Only the vertex stage reads nothing here, but the block must still match.
layout(push_constant) uniform SponzaGeomPC {
    vec4 base_color_factor;
    uint tex_base_color_index;
    uint tex_normal_index;
    uint tex_specular_index;
    uint tex_mask_index;
    uint material_flags; // bit0: normal map, bit1: alpha mask, bit2: specular map
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

    // The Khronos Sponza tangents are not strictly perpendicular to the normals
    // (about 16% of vertices exceed a 0.1 dot product), so re-orthogonalise here
    // with Gram-Schmidt instead of trusting the authored frame.
    vec3 n = normalize(inNormal);
    vec3 t = normalize(inTangent.xyz - n * dot(n, inTangent.xyz));

    v_normal = n;
    v_tangent = t;
    v_bitangent = cross(n, t) * inTangent.w;
    v_texcoord = inTexCoord;
}
