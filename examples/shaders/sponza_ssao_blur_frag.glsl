#version 450

// Cross-bilateral blur for the SSAO buffer.
//
// The SSAO pass rotates its kernel per pixel, so the raw result is noisy. A
// plain box blur would bleed occlusion across silhouettes, so samples are
// weighted by depth similarity and only neighbours on the same surface count.

#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 3, binding = 0) uniform SponzaSsaoBlurShared {
    vec4 params; // xy: texel size, z: depth rejection scale, w: unused
} fsp;

layout(push_constant) uniform SponzaSsaoBlurPC {
    uint gAoIndex;
    uint gDepthIndex;
} fpc;

layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

#define gAo    combinedTextureSamplerHandles[fpc.gAoIndex]
#define gDepth combinedTextureSamplerHandles[fpc.gDepthIndex]

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 outAo;

void main()
{
    float centerDepth = texture(gDepth, v_texcoord).x;
    if (centerDepth >= 1.0)
    {
        outAo = vec4(1.0);
        return;
    }

    vec2 texel = fsp.params.xy;
    float rejection = fsp.params.z;

    float total = 0.0;
    float weightSum = 0.0;
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            vec2 uv = v_texcoord + vec2(x, y) * texel;
            float depth = texture(gDepth, uv).x;
            float weight = exp(-abs(depth - centerDepth) * rejection);
            total += texture(gAo, uv).r * weight;
            weightSum += weight;
        }
    }

    outAo = vec4(total / max(weightSum, 1e-4), 0.0, 0.0, 1.0);
}
