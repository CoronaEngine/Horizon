#version 450

// Port of bgfx examples/36-sky fs_sky_color_banding_fix.sc

layout(set = 3, binding = 0) uniform SkyShared {
    mat4 invViewProj;
    vec4 sunDirection;
    vec4 skyLuminanceXYZ;
    vec4 sunLuminance;
    vec4 parameters; // x: sun size, y: sun bloom, z: exposure, w: time
    vec4 perez0;
    vec4 perez1;
    vec4 perez2;
    vec4 perez3;
    vec4 perez4;
} sky_fs;

layout(location = 0) in vec3 v_skyColor;
layout(location = 1) in vec2 v_screenPos;
layout(location = 2) in vec3 v_viewDir;

layout(location = 0) out vec4 outColor;

float interleavedGradientNoise(vec2 screenPos, float temporalFactor)
{
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    vec2 coord = screenPos + fract(temporalFactor) * vec2(13.0);
    return fract(magic.z * fract(dot(coord, magic.xy)));
}

vec3 toGamma(vec3 color)
{
    return pow(abs(color), vec3(1.0 / 2.2));
}

void main()
{
    float size2 = sky_fs.parameters.x * sky_fs.parameters.x;

    vec3 lightDir = normalize(sky_fs.sunDirection.xyz);
    float dist = 2.0 * (1.0 - dot(normalize(v_viewDir), lightDir));
    float sun = exp(-dist / sky_fs.parameters.y / size2) + step(dist, size2);
    float sun2 = min(sun * sun, 1.0);

    vec3 color = toGamma(v_skyColor + vec3(sun2));

    float dither = interleavedGradientNoise(gl_FragCoord.xy, sky_fs.parameters.w);
    dither = (dither - 0.5) / 255.0;
    color += vec3(dither);

    outColor = vec4(color, 1.0);
}
