#version 450

// Sponza sun shadow map, vertex stage: depth-only render from the light.
//
// The vertex layout has to match the shared HZMS buffer even though only
// position and uv are used, because the same vertex buffer feeds this pass and
// the G-buffer pass.

layout(set = 3, binding = 0) uniform SponzaShadowShared {
    mat4 light_view_proj;
} vsp;

layout(push_constant) uniform SponzaShadowPC {
    uint tex_mask_index;
    uint material_flags; // bit1: alpha mask (same bit as the G-buffer pass)
} model_pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec2 v_texcoord;

void main()
{
    // Node transforms are baked into the vertices, so object space is world space.
    gl_Position = vsp.light_view_proj * vec4(inPosition, 1.0);
    v_texcoord = inTexCoord;
}
