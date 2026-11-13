#version 450
#include "raster_batch.slang"

// Port of bgfx examples/36-sky vs_sky_landscape.sc

layout(set = 3, binding = 0) uniform LandscapeShared {
    mat4 viewProj;
    vec4 sunDirection;
    vec4 sunLuminance;
    vec4 skyLuminance;
    vec4 parameters; // z: exposure, w: time
    mat4 model;
    uint lightmapIndex;
} ls;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec2 v_texcoord0;

void main()
{
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + gl_DrawID];
    uint* vertex = hzPullVertex(packet, gl_VertexIndex);
    vec3 inPosition = *(vec3*)(vertex + 0);
    vec3 inNormal = *(vec3*)(vertex + 3);
    vec2 inTexCoord = *(vec2*)(vertex + 6);
    mat4 mvp = ls.viewProj * ls.model;
    gl_Position = mvp * vec4(inPosition, 1.0);

    // bgfx mesh stores normals as uint8 in [0,1]; CPU loader already expands to [-1,1].
    v_normal = (ls.model * vec4(inNormal, 0.0)).xyz;
    v_texcoord0 = inTexCoord;
}
