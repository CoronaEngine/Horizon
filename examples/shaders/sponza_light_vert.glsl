#version 450

// Sponza deferred light accumulation, vertex stage.
//
// Same screen-rect trick as deferred_light_vert.glsl: the quad corners are
// remapped onto the light's NDC bounding rect so each light only touches the
// pixels it can influence. Directional lights pass a full-screen rect.

// set 0-2 are reserved for Horizon bindless tables, so plain UBOs go in set 3.
layout(set = 3, binding = 0) uniform SponzaLightShared {
    mat4 inv_view_proj;
    mat4 view;
    vec4 camera_pos;
    mat4 sun_view_proj;
    mat4 inv_sun_view_proj;
    vec4 shadow_params;
    vec4 rsm_params;
    vec4 disney0;
    vec4 disney1;
} vsp;

// Shared with sponza_light_frag.glsl; the layout must stay identical.
layout(push_constant) uniform SponzaLightPC {
    vec4 light_pos_radius;  // xyz: position, or direction when w < 0
    vec4 light_rgb_inner_r; // rgb: colour, w: inner falloff
    vec4 rect;              // xy: NDC min, zw: NDC max
    uint gAlbedoIndex;
    uint gNormalIndex;
    uint gDepthIndex;
    uint gShadowIndex;
    uint rsmNormalIndex;
    uint rsmFluxIndex;
} vpc;

layout(location = 0) in vec3 inCorner;

layout(location = 0) out vec2 v_texcoord;

void main()
{
    vec2 ndc = mix(vpc.rect.xy, vpc.rect.zw, inCorner.xy);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_texcoord = ndc * 0.5 + 0.5;
}
