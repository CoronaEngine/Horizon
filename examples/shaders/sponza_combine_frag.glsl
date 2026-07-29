#version 450

// Sponza deferred combine.
//
//   colour = albedo * (diffuseLight + irradiance * AO)
//          + specularLight * specularMask
//          + radiance * fresnel * AO
//
// The light buffer is linear (see sponza_light_frag.glsl), so only the albedo
// and the environment probe need converting out of sRGB. Direct specular is
// added after the albedo multiply so highlights stay the colour of the light.
//
// Ambient occlusion multiplies only the two indirect terms. Direct light already
// carries its own visibility (the sun through the shadow map, the point lights
// through their falloff), so applying AO there would double-count.

#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 3, binding = 0) uniform SponzaCombineShared {
    mat4 inv_view_proj;
    vec4 camera_pos;
    vec4 env_params;   // x: irradiance scale, y: radiance scale, z: env mip count, w: unused
    vec4 metal_params; // x: metalness threshold low, y: high, z: tint amount, w: unused
} fsp;

layout(push_constant) uniform SponzaCombinePC {
    vec4 ambient; // rgb: constant ambient floor, w: exposure
    uint gAlbedoIndex;
    uint gLightIndex;
    uint gNormalIndex;
    uint gAoIndex;
    uint gDepthIndex;
    uint texCubeIrrIndex;
    uint texCubeLodIndex;
    uint debugMode;
} fpc;

layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];
// Cube views live in the same bindless table as the 2D views: the descriptor
// type is identical (COMBINED_IMAGE_SAMPLER) and the VkImageView decides the
// view type, so the two aliases share one index space.
layout(set = 0, binding = 0) uniform samplerCube combinedCubeSamplerHandles[];

#define gAlbedo    combinedTextureSamplerHandles[fpc.gAlbedoIndex]
#define gLight     combinedTextureSamplerHandles[fpc.gLightIndex]
#define gNormal    combinedTextureSamplerHandles[fpc.gNormalIndex]
#define gAo        combinedTextureSamplerHandles[fpc.gAoIndex]
#define gDepth     combinedTextureSamplerHandles[fpc.gDepthIndex]
#define texCubeIrr combinedCubeSamplerHandles[fpc.texCubeIrrIndex]
#define texCubeLod combinedCubeSamplerHandles[fpc.texCubeLodIndex]

#define DEBUG_FINAL     0u
#define DEBUG_ALBEDO    1u
#define DEBUG_NORMAL    2u
#define DEBUG_DIFFUSE   3u
#define DEBUG_SPECULAR  4u
#define DEBUG_SPECMASK  5u
#define DEBUG_GLOSS     6u
#define DEBUG_AO        7u
#define DEBUG_INDIRECT  8u
#define DEBUG_METALNESS 9u

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 outColor;

// Hejl-Burgess-Dawson filmic curve, the same one example_ibl uses. Linear light
// from dozens of additive sources routinely exceeds 1.0, so the result has to be
// tone mapped rather than clamped. The curve already includes the sRGB encode,
// so no separate gamma step follows it.
vec3 toFilmic(vec3 rgb)
{
    rgb = max(vec3(0.0), rgb - 0.004);
    return (rgb * (6.2 * rgb + 0.5)) / (rgb * (6.2 * rgb + 1.7) + 0.06);
}

vec3 toLinear(vec3 rgb) { return pow(abs(rgb), vec3(2.2)); }

vec3 calcFresnel(vec3 cspec, float dotNV, float strength)
{
    return cspec + (1.0 - cspec) * pow(1.0 - dotNV, 5.0) * strength;
}

vec3 worldFromDepth(vec2 uv, float deviceDepth)
{
    vec4 wpos = fsp.inv_view_proj * vec4(uv * 2.0 - 1.0, deviceDepth, 1.0);
    return wpos.xyz / wpos.w;
}

void main()
{
    vec4 albedoSample = texture(gAlbedo, v_texcoord);
    vec4 light = texture(gLight, v_texcoord);
    vec4 packedNormal = texture(gNormal, v_texcoord);
    float deviceDepth = texture(gDepth, v_texcoord).x;
    float ao = texture(gAo, v_texcoord).r;

    vec3 albedo = pow(albedoSample.rgb, vec3(2.2)); // toLinear
    float specularMask = albedoSample.a;
    float gloss = packedNormal.w;

    if (deviceDepth >= 1.0)
    {
        // Nothing was rasterised: show the environment through the open roof
        // instead of a flat clear colour.
        vec3 dir = normalize(worldFromDepth(v_texcoord, 1.0) - fsp.camera_pos.xyz);
        vec3 sky = toLinear(texture(texCubeLod, dir).rgb) * fsp.env_params.y;
        outColor = vec4(toFilmic(sky * fpc.ambient.w), 1.0);
        return;
    }

    vec3 normal = normalize(packedNormal.xyz * 2.0 - 1.0);
    vec3 wpos = worldFromDepth(v_texcoord, deviceDepth);
    vec3 viewDir = normalize(fsp.camera_pos.xyz - wpos);
    float ndotv = clamp(dot(normal, viewDir), 0.0, 1.0);

    // Indirect diffuse from the irradiance probe, indirect specular from the
    // pre-filtered radiance probe with the mip chosen by glossiness.
    vec3 irradiance = toLinear(texture(texCubeIrr, normal).rgb) * fsp.env_params.x;

    vec3 reflected = reflect(-viewDir, normal);
    float mip = (1.0 - gloss) * max(0.0, fsp.env_params.z - 1.0);
    vec3 radiance = toLinear(textureLod(texCubeLod, reflected, mip).rgb) * fsp.env_params.y;

    // Sponza's glTF has one constant metallicFactor for all 25 materials, so
    // metalness has to be inferred. The '*_spec' map is the available proxy: the
    // flagpoles, chains and metal details sit high in it, plaster and fabric low.
    // Metals reflect their own colour, which is what actually makes them read as
    // metal, so the specular colour is tinted towards the albedo as spec rises.
    float metalness = smoothstep(fsp.metal_params.x, fsp.metal_params.y, specularMask)
                      * fsp.metal_params.z;
    vec3 specTint = mix(vec3(1.0), albedo, metalness);
    vec3 cspec = mix(vec3(0.03), albedo, metalness) + vec3(0.22 * specularMask);
    vec3 envFresnel = calcFresnel(cspec, ndotv, gloss);

    vec3 indirectDiffuse = albedo * (irradiance + fpc.ambient.xyz) * ao;
    vec3 indirectSpecular = radiance * envFresnel * ao;

    switch (fpc.debugMode)
    {
    case DEBUG_ALBEDO:   outColor = vec4(albedoSample.rgb, 1.0); return;
    case DEBUG_NORMAL:   outColor = vec4(packedNormal.xyz, 1.0); return;
    case DEBUG_DIFFUSE:  outColor = vec4(light.rgb * 0.25, 1.0); return;
    case DEBUG_SPECULAR: outColor = vec4(vec3(light.a), 1.0); return;
    case DEBUG_SPECMASK: outColor = vec4(vec3(specularMask), 1.0); return;
    case DEBUG_GLOSS:    outColor = vec4(vec3(gloss), 1.0); return;
    case DEBUG_AO:       outColor = vec4(vec3(ao), 1.0); return;
    case DEBUG_INDIRECT: outColor = vec4(toFilmic((indirectDiffuse + indirectSpecular)
                                                  * fpc.ambient.w), 1.0); return;
    case DEBUG_METALNESS: outColor = vec4(specTint, 1.0); return;
    default: break;
    }

    vec3 color = albedo * light.rgb + light.a * specularMask * specTint
                 + indirectDiffuse + indirectSpecular;

    outColor = vec4(toFilmic(color * fpc.ambient.w), 1.0);
}
