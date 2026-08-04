#version 450

// Cross-bilateral blur for the SSAO buffer.
//
// The SSAO pass rotates its kernel per pixel, so the raw result is noisy. A
// plain box blur would bleed occlusion across silhouettes, so samples are
// weighted by depth similarity and only neighbours on the same surface count.

#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 3, binding = 0) uniform SponzaSsaoBlurShared {
    vec4 params;       // xy: texel size, z: view-depth sigma, w: normal exponent
    vec4 depth_unpack; // viewZ = x / (deviceDepth + y)
} fsp;

layout(push_constant) uniform SponzaSsaoBlurPC {
    uint gAoIndex;
    uint gDepthIndex;
    uint gNormalIndex;
} fpc;

layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

#define gAo    combinedTextureSamplerHandles[fpc.gAoIndex]
#define gDepth combinedTextureSamplerHandles[fpc.gDepthIndex]
#define gNormal combinedTextureSamplerHandles[fpc.gNormalIndex]

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 outAo;

float viewDepth(float deviceDepth)
{
    return fsp.depth_unpack.x / (deviceDepth + fsp.depth_unpack.y);
}

void main()
{
    float centerDepth = texture(gDepth, v_texcoord).x;
    if (centerDepth >= 1.0)
    {
        outAo = vec4(1.0);
        return;
    }

    vec2 texel = fsp.params.xy;
    float centerViewDepth = viewDepth(centerDepth);
    float depthSigma = max(fsp.params.z, 1e-4);
    vec3 centerNormal = normalize(texture(gNormal, v_texcoord).xyz * 2.0 - 1.0);

    float total = 0.0;
    float weightSum = 0.0;
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            vec2 uv = clamp(v_texcoord + vec2(x, y) * texel,
                            texel * 0.5, vec2(1.0) - texel * 0.5);
            float depth = texture(gDepth, uv).x;
            if (depth >= 1.0)
                continue;

            float depthDelta = (viewDepth(depth) - centerViewDepth) / depthSigma;
            float depthWeight = exp(-0.5 * depthDelta * depthDelta);
            vec3 normal = normalize(texture(gNormal, uv).xyz * 2.0 - 1.0);
            float normalWeight = pow(max(dot(centerNormal, normal), 0.0), fsp.params.w);
            float spatialWeight = exp(-0.5 * float(x * x + y * y) / 4.0);
            float weight = depthWeight * normalWeight * spatialWeight;
            total += texture(gAo, uv).r * weight;
            weightSum += weight;
        }
    }

    outAo = vec4(total / max(weightSum, 1e-4), 0.0, 0.0, 1.0);
}
