#version 450
#extension GL_EXT_nonuniform_qualifier : enable

// Port of bgfx examples/36-sky fs_sky_landscape.sc

layout(set = 3, binding = 0) uniform LandscapeShared {
    mat4 viewProj;
    vec4 sunDirection;
    vec4 sunLuminance;
    vec4 skyLuminance;
    vec4 parameters; // z: exposure, w: time
} ls_fs;

layout(push_constant) uniform LandscapePC {
    mat4 model;
    uint lightmapIndex;
} pc_fs;

layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec2 v_texcoord0;

layout(location = 0) out vec4 outColor;

float interleavedGradientNoise(vec2 screenPos, float temporalFactor)
{
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    vec2 coord = screenPos + fract(temporalFactor) * vec2(13.0);
    return fract(magic.z * fract(dot(coord, magic.xy)));
}

float toLinear(float rgb)
{
    return pow(abs(rgb), 2.2);
}

vec3 toGamma(vec3 color)
{
    return pow(abs(color), vec3(1.0 / 2.2));
}

void main()
{
    vec3 normal = normalize(v_normal);
    float occlusion = toLinear(texture(combinedTextureSamplerHandles[pc_fs.lightmapIndex], v_texcoord0).r);

    // Match bgfx landscape hemisphere axis (Z-up in that shader).
    vec3 skyDirection = vec3(0.0, 0.0, 1.0);

    float diffuseSun = max(0.0, dot(normal, normalize(ls_fs.sunDirection.xyz)));
    float diffuseSky = 1.0 + 0.5 * dot(normal, skyDirection);

    vec3 color = diffuseSun * ls_fs.sunLuminance.rgb + (diffuseSky * ls_fs.skyLuminance.rgb + 0.01) * occlusion;
    color *= 0.5;
    color *= ls_fs.parameters.z;

    float dither = interleavedGradientNoise(gl_FragCoord.xy, ls_fs.parameters.w);
    dither = (dither - 0.5) / 255.0;

    outColor = vec4(toGamma(color) + vec3(dither), 1.0);
}
