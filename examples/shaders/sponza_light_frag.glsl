#version 450

// Sponza deferred light accumulation, fragment stage.
//
// Follows deferred_light_frag.glsl: world position is rebuilt from the G-buffer
// depth value, then a light contribution is accumulated additively. Two
// deliberate differences from that example:
//   - accumulation is linear, not gamma-encoded per light. The target is
//     RGBA16F, so there is no reason to round-trip through sRGB on every light,
//     and adding gamma-encoded values is not physically meaningful.
//   - diffuse goes to rgb and specular to alpha, so the combine pass can add the
//     highlight after the albedo multiply instead of tinting it.

#extension GL_EXT_nonuniform_qualifier : enable

// set 0-2 are reserved for Horizon bindless tables, so plain UBOs go in set 3.
layout(set = 3, binding = 0) uniform SponzaLightShared {
    mat4 inv_view_proj;
    mat4 view;
    vec4 camera_pos;
    mat4 sun_view_proj;
    vec4 shadow_params; // x: world size of one shadow texel, y: depth bias,
                        // z: shadow map resolution, w: 1 to enable sun shadows
    vec4 light_params;  // x: specular strength, yzw: unused
} fsp;

// Shares the push constant block with sponza_light_vert.glsl.
layout(push_constant) uniform SponzaLightPC {
    vec4 light_pos_radius;
    vec4 light_rgb_inner_r;
    vec4 rect;
    uint gNormalIndex;
    uint gDepthIndex;
    uint gShadowIndex;
} fpc;

layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

#define gNormal combinedTextureSamplerHandles[fpc.gNormalIndex]
#define gDepth  combinedTextureSamplerHandles[fpc.gDepthIndex]
#define gShadow combinedTextureSamplerHandles[fpc.gShadowIndex]

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 outColor;

// 3x3 PCF against the R32F sun shadow map. The receiver is pushed along its
// normal by one shadow texel before projecting, which removes the acne that a
// pure depth bias cannot fix on surfaces nearly parallel to the light.
float sunShadow(vec3 wpos, vec3 normal)
{
    if (fsp.shadow_params.w < 0.5)
        return 1.0;

    vec3 offsetPos = wpos + normal * fsp.shadow_params.x;
    vec4 clip = fsp.sun_view_proj * vec4(offsetPos, 1.0);
    vec3 ndc = clip.xyz / clip.w;

    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) ||
        ndc.z < 0.0 || ndc.z > 1.0)
        return 1.0; // outside the shadow map's coverage

    float receiver = ndc.z - fsp.shadow_params.y;
    float texel = 1.0 / fsp.shadow_params.z;

    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            float occluder = texture(gShadow, uv + vec2(x, y) * texel).r;
            visibility += step(receiver, occluder);
        }
    }
    return visibility / 9.0;
}

void main()
{
    vec4 packedNormal = texture(gNormal, v_texcoord);
    float deviceDepth = texture(gDepth, v_texcoord).x;
    if (deviceDepth >= 1.0)
        discard; // nothing was rasterised here

    vec3 normal = normalize(packedNormal.xyz * 2.0 - 1.0);
    float gloss = packedNormal.w;

    vec3 clip = vec3(v_texcoord * 2.0 - 1.0, deviceDepth);
    vec4 wpos4 = fsp.inv_view_proj * vec4(clip, 1.0);
    vec3 wpos = wpos4.xyz / wpos4.w;

    vec3 viewDir = normalize(fsp.camera_pos.xyz - wpos);

    vec3 lightDir;
    float attenuation;
    if (fpc.light_pos_radius.w < 0.0)
    {
        // Directional: xyz is the direction the light travels.
        lightDir = -normalize(fpc.light_pos_radius.xyz);
        // Only the sun is shadowed; the animated point lights are not, which is
        // what keeps this a single extra pass rather than one per light.
        attenuation = sunShadow(wpos, normal);
    }
    else
    {
        vec3 lp = fpc.light_pos_radius.xyz - wpos;
        attenuation = 1.0 - smoothstep(fpc.light_rgb_inner_r.w, 1.0,
                                       length(lp) / fpc.light_pos_radius.w);
        lightDir = normalize(lp);
    }

    float ndotl = max(0.0, dot(normal, lightDir));
    vec3 diffuse = fpc.light_rgb_inner_r.xyz * ndotl * attenuation;

    // Scaled by gloss as well as raised to a gloss-driven power: Sponza's
    // materials are mostly rough (gloss ~0.1), which gives a very broad lobe.
    // Without the linear factor that lobe behaves like a second diffuse term and
    // washes rough surfaces out.
    vec3 halfVec = normalize(lightDir + viewDir);
    float specPower = exp2(1.0 + 9.0 * gloss);
    float specular = pow(max(0.0, dot(normal, halfVec)), specPower)
                     * ndotl * attenuation * gloss * fsp.light_params.x;

    outColor = vec4(diffuse, specular);
}
