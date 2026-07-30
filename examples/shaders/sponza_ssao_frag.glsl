#version 450

// Sponza screen-space ambient occlusion.
//
// A hemisphere-kernel SSAO over the existing G-buffer rather than a port of
// example_assao: that example runs the full ASSAO compute chain, which needs its
// own depth mip pyramid and half-resolution deinterleaving. Here the G-buffer
// already carries world normals and a device-depth target, so a single
// full-screen pass is enough and stays readable.
//
// Occlusion only ever modulates indirect light in the combine pass; direct light
// is left alone, since darkening a directly lit surface by AO is not physical.

#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 3, binding = 0) uniform SponzaSsaoShared {
    mat4 inv_view_proj;
    mat4 view;
    mat4 proj;
    vec4 params; // x: radius (world units), y: bias, z: intensity, w: unused
} fsp;

layout(push_constant) uniform SponzaSsaoPC {
    uint gNormalIndex;
    uint gDepthIndex;
} fpc;

layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

#define gNormal combinedTextureSamplerHandles[fpc.gNormalIndex]
#define gDepth  combinedTextureSamplerHandles[fpc.gDepthIndex]

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 outAo;

const int sample_count = 16;

// Golden-spiral hemisphere directions, scaled so samples cluster near the origin
// (equivalent to the usual lerp-weighted kernel, without a uniform buffer).
vec3 kernelSample(int i)
{
    float fi = float(i) + 0.5;
    float cosTheta = 1.0 - fi / float(sample_count); // 1 -> 0, hemisphere
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = fi * 2.39996323; // golden angle
    vec3 dir = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    float scale = 0.3 + 0.7 * (fi / float(sample_count)) * (fi / float(sample_count));
    return dir * scale;
}

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 worldFromDepth(vec2 uv, float deviceDepth)
{
    vec4 wpos = fsp.inv_view_proj * vec4(uv * 2.0 - 1.0, deviceDepth, 1.0);
    return wpos.xyz / wpos.w;
}

void main()
{
    float deviceDepth = texture(gDepth, v_texcoord).x;
    if (deviceDepth >= 1.0)
    {
        outAo = vec4(1.0); // background is never occluded
        return;
    }

    vec3 wpos = worldFromDepth(v_texcoord, deviceDepth);
    vec3 normal = normalize(texture(gNormal, v_texcoord).xyz * 2.0 - 1.0);

    // Random-rotated tangent frame, so the 16 samples decorrelate between pixels
    // and the blur pass can resolve them into smooth occlusion.
    float angle = hash12(gl_FragCoord.xy) * 6.2831853;
    vec3 randomVec = vec3(cos(angle), sin(angle), 0.0);
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 tbn = mat3(tangent, bitangent, normal);

    float radius = fsp.params.x;
    float bias = fsp.params.y;
    float occlusion = 0.0;

    for (int i = 0; i < sample_count; ++i)
    {
        vec3 samplePos = wpos + tbn * kernelSample(i) * radius;

        vec4 clip = fsp.proj * fsp.view * vec4(samplePos, 1.0);
        if (clip.w <= 0.0)
            continue;
        vec3 ndc = clip.xyz / clip.w;
        vec2 uv = ndc.xy * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
            continue;

        float sceneDepth = texture(gDepth, uv).x;
        if (sceneDepth >= 1.0)
            continue;

        vec3 sceneWorld = worldFromDepth(uv, sceneDepth);

        // Compare along the view direction: the sample is occluded when the
        // surface actually rendered there sits closer to the camera than it.
        float sampleViewZ = (fsp.view * vec4(samplePos, 1.0)).z;
        float sceneViewZ = (fsp.view * vec4(sceneWorld, 1.0)).z;

        if (sceneViewZ < sampleViewZ - bias)
        {
            // Ignore occluders far outside the sampling radius, which would
            // otherwise draw dark halos around foreground silhouettes.
            float rangeCheck = smoothstep(0.0, 1.0, radius / max(1e-4, abs(sampleViewZ - sceneViewZ)));
            occlusion += rangeCheck;
        }
    }

    float ao = 1.0 - (occlusion / float(sample_count)) * fsp.params.z;
    outAo = vec4(clamp(ao, 0.0, 1.0), 0.0, 0.0, 1.0);
}
