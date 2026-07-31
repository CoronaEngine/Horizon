#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

// Final Sponza post pass: composite SSR in linear HDR, add restrained
// highlight-only bloom, then exposure, ACES, color grading and vignette.
// Analytical combine debug views bypass this entire post stack.
layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 2, binding = 0, rgba16f) uniform image2D imagesRGBA16F[];

layout(push_constant) uniform PushConsts
{
    uint colorID;   // RGBA16F linear HDR, alpha is SSR reflectivity
    uint ssrID;     // RGBA16F reflected color, alpha is SSR weight
    uint outputID;  // RGBA16F display output
    uint pad0;
    vec4 params0;   // xy: resolution, z: SSR intensity, w: debug passthrough
    vec4 params1;   // x: exposure, y: SSR debug mode, zw: unused
    vec4 params2;   // x: bloom threshold, y: intensity, z: radius, w: vignette
    vec4 grade;     // x: contrast, y: saturation, z: temperature, w: unused
} pushConsts;

vec3 toFilmic(vec3 rgb)
{
    rgb = max(rgb, vec3(0.0));
    vec3 aces = clamp((rgb * (2.51 * rgb + 0.03)) /
                      (rgb * (2.43 * rgb + 0.59) + 0.14), 0.0, 1.0);
    return pow(aces, vec3(1.0 / 2.2));
}

float luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 bloomSample(ivec2 coord)
{
    ivec2 limit = ivec2(pushConsts.params0.xy) - ivec2(1);
    vec3 color = max(imageLoad(imagesRGBA16F[pushConsts.colorID],
                               clamp(coord, ivec2(0), limit)).rgb, vec3(0.0));
    float value = luminance(color);
    float threshold = pushConsts.params2.x;
    float knee = max(threshold * 0.5, 1e-4);
    float soft = clamp(value - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float contribution = max(value - threshold, 0.0) + soft;
    return color * (contribution / max(value, 1e-4));
}

vec3 gatherBloom(ivec2 coord)
{
    int r = max(1, int(pushConsts.params2.z + 0.5));
    vec3 bloom = bloomSample(coord) * 0.20;

    bloom += bloomSample(coord + ivec2( r,  0)) * 0.09;
    bloom += bloomSample(coord + ivec2(-r,  0)) * 0.09;
    bloom += bloomSample(coord + ivec2( 0,  r)) * 0.09;
    bloom += bloomSample(coord + ivec2( 0, -r)) * 0.09;

    bloom += bloomSample(coord + ivec2( r,  r)) * 0.06;
    bloom += bloomSample(coord + ivec2(-r,  r)) * 0.06;
    bloom += bloomSample(coord + ivec2( r, -r)) * 0.06;
    bloom += bloomSample(coord + ivec2(-r, -r)) * 0.06;

    bloom += bloomSample(coord + ivec2( 2 * r, 0)) * 0.05;
    bloom += bloomSample(coord + ivec2(-2 * r, 0)) * 0.05;
    bloom += bloomSample(coord + ivec2(0,  2 * r)) * 0.05;
    bloom += bloomSample(coord + ivec2(0, -2 * r)) * 0.05;
    return bloom;
}

vec3 colorGrade(vec3 color)
{
    // This is an intentionally small art control, not a chromatic adaptation.
    color *= vec3(1.0 + pushConsts.grade.z * 0.12,
                  1.0,
                  1.0 - pushConsts.grade.z * 0.12);
    float luma = luminance(color);
    color = mix(vec3(luma), color, pushConsts.grade.y);
    return clamp((color - 0.5) * pushConsts.grade.x + 0.5, 0.0, 1.0);
}

void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= int(pushConsts.params0.x) || coord.y >= int(pushConsts.params0.y))
        return;

    vec4 base = imageLoad(imagesRGBA16F[pushConsts.colorID], coord);
    vec4 ssr = imageLoad(imagesRGBA16F[pushConsts.ssrID], coord);

    if (pushConsts.params0.w > 0.5)
    {
        imageStore(imagesRGBA16F[pushConsts.outputID], coord, vec4(base.rgb, 1.0));
        return;
    }

    float weight = clamp(ssr.w * pushConsts.params0.z, 0.0, 1.0);
    vec3 result = mix(base.rgb, ssr.rgb, weight);

    int mode = int(pushConsts.params1.y + 0.5);
    if (mode == 1)
        result = ssr.rgb;
    else if (mode == 2)
    {
        imageStore(imagesRGBA16F[pushConsts.outputID], coord, vec4(vec3(weight), 1.0));
        return;
    }
    else if (mode == 3)
    {
        imageStore(imagesRGBA16F[pushConsts.outputID], coord, vec4(vec3(base.w), 1.0));
        return;
    }

    if (mode == 0 && pushConsts.params2.y > 0.0)
        result += gatherBloom(coord) * pushConsts.params2.y;

    vec3 outputColor = colorGrade(toFilmic(result * pushConsts.params1.x));
    vec2 uv = (vec2(coord) + 0.5) / pushConsts.params0.xy;
    vec2 centered = uv * 2.0 - 1.0;
    centered.x *= pushConsts.params0.x / pushConsts.params0.y;
    float vignette = 1.0 - pushConsts.params2.w *
                     smoothstep(0.35, 1.35, length(centered));
    outputColor *= vignette;

    imageStore(imagesRGBA16F[pushConsts.outputID], coord, vec4(outputColor, 1.0));
}
