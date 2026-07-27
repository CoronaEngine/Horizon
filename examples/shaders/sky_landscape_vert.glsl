#version 450

// Port of bgfx examples/36-sky vs_sky_landscape.sc

layout(set = 3, binding = 0) uniform LandscapeShared {
    mat4 viewProj;
    vec4 sunDirection;
    vec4 sunLuminance;
    vec4 skyLuminance;
    vec4 parameters; // z: exposure, w: time
} ls;

layout(push_constant) uniform LandscapePC {
    mat4 model;
    uint lightmapIndex;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec2 v_texcoord0;

void main()
{
    mat4 mvp = ls.viewProj * pc.model;
    gl_Position = mvp * vec4(inPosition, 1.0);

    // bgfx mesh stores normals as uint8 in [0,1]; CPU loader already expands to [-1,1].
    v_normal = (pc.model * vec4(inNormal, 0.0)).xyz;
    v_texcoord0 = inTexCoord;
}
