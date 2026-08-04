#version 450

// Sponza deferred combine —— PBR 版。
//
//   hdr = light.rgb                                  (直接光 radiance,Disney,已含 albedo)
//       + albedo * (irradiance + ambient) * AO * (1-metallic)   (IBL 漫反射)
//       + radiance * F(cspec, NdotV) * AO                       (IBL 镜面)
//
// 与旧版的两个结构性变化:
//   - light buffer 已是完整 radiance(Disney BRDF 在 light pass 算完),
//     这里不再给直接光乘 albedo;
//   - 输出改为线性 HDR(不做曝光和 filmic),a 通道写 SSR 用的 reflectivity
//     = mix(0.04, 1.0, metallic)。曝光 + tone map 移到 SSR composite
//     (sponza_ssr_composite_compute.glsl),反射要在线性空间里采样与混合。
//
// debugMode != 0 时输出显示就绪的调试视图,SSR composite 原样透传。

#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 3, binding = 0) uniform SponzaCombineShared {
    mat4 inv_view_proj;
    vec4 camera_pos;
    vec4 env_params;   // x: irradiance scale, y: radiance scale, z: env mip count,
                       // w: environment rotation around world Y (radians)
} fsp;

layout(push_constant) uniform SponzaCombinePC {
    vec4 ambient; // rgb: constant ambient floor, w: unused(曝光已移到 composite)
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
#define DEBUG_DIRECT    3u
#define DEBUG_METALLIC  4u
#define DEBUG_ROUGHNESS 5u
#define DEBUG_AO        6u
#define DEBUG_INDIRECT  7u

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 outColor;

vec3 toFilmic(vec3 rgb)
{
    rgb = max(rgb, vec3(0.0));
    vec3 aces = clamp((rgb * (2.51 * rgb + 0.03)) /
                      (rgb * (2.43 * rgb + 0.59) + 0.14), 0.0, 1.0);
    return pow(aces, vec3(1.0 / 2.2));
}

vec3 calcFresnel(vec3 cspec, float dotNV, float strength)
{
    return cspec + (1.0 - cspec) * pow(1.0 - dotNV, 5.0) * strength;
}

vec3 worldFromDepth(vec2 uv, float deviceDepth)
{
    vec4 wpos = fsp.inv_view_proj * vec4(uv * 2.0 - 1.0, deviceDepth, 1.0);
    return wpos.xyz / wpos.w;
}

vec3 environmentDirection(vec3 worldDirection)
{
    // Positive UI rotation turns the environment in world space, therefore
    // sampling applies the inverse rotation.
    float c = cos(fsp.env_params.w);
    float s = sin(fsp.env_params.w);
    return vec3(c * worldDirection.x - s * worldDirection.z,
                worldDirection.y,
                s * worldDirection.x + c * worldDirection.z);
}

void main()
{
    vec4 albedoSample = texture(gAlbedo, v_texcoord);
    vec4 light = texture(gLight, v_texcoord);
    vec4 packedNormal = texture(gNormal, v_texcoord);
    float deviceDepth = texture(gDepth, v_texcoord).x;
    float ao = texture(gAo, v_texcoord).r;

    vec3 albedo = albedoSample.rgb; // already linear (BC7_SRGB sampling)
    float metallic = albedoSample.a;
    float roughness = packedNormal.w;

    if (deviceDepth >= 1.0)
    {
        // Nothing was rasterised: show the environment through the open roof.
        vec3 dir = normalize(worldFromDepth(v_texcoord, 1.0) - fsp.camera_pos.xyz);
        vec3 sky = texture(texCubeLod, environmentDirection(dir)).rgb * fsp.env_params.y;
        outColor = vec4(sky, 0.0); // 天空不参与 SSR
        return;
    }

    vec3 normal = normalize(packedNormal.xyz * 2.0 - 1.0);
    vec3 wpos = worldFromDepth(v_texcoord, deviceDepth);
    vec3 viewDir = normalize(fsp.camera_pos.xyz - wpos);
    float ndotv = clamp(dot(normal, viewDir), 0.0, 1.0);

    // Indirect diffuse from the irradiance probe, indirect specular from the
    // pre-filtered radiance probe with the mip chosen by roughness.
    vec3 irradiance = texture(texCubeIrr, environmentDirection(normal)).rgb * fsp.env_params.x;

    vec3 reflected = reflect(-viewDir, normal);
    float mip = roughness * max(0.0, fsp.env_params.z - 1.0);
    vec3 radiance = textureLod(texCubeLod, environmentDirection(reflected), mip).rgb
                    * fsp.env_params.y;

    vec3 cspec = mix(vec3(0.04), albedo, metallic);
    vec3 envFresnel = calcFresnel(cspec, ndotv, 1.0 - roughness * 0.7);

    vec3 indirectDiffuse = albedo * (1.0 - metallic) * (irradiance + fpc.ambient.xyz) * ao;
    // Smooth reflections should not be blackened by crevice AO. Blend towards
    // AO only as the specular lobe gets rougher and more local.
    float specularOcclusion = mix(1.0, ao, roughness);
    // The scene mesh is diffuse-only. Only the separately authored metallic
    // mirror pool receives IBL specular.
    vec3 indirectSpecular = radiance * envFresnel * specularOcclusion * metallic;

    switch (fpc.debugMode)
    {
    case DEBUG_ALBEDO:    outColor = vec4(albedoSample.rgb, 0.0); return;
    case DEBUG_NORMAL:    outColor = vec4(packedNormal.xyz, 0.0); return;
    case DEBUG_DIRECT:    outColor = vec4(toFilmic(light.rgb), 0.0); return;
    case DEBUG_METALLIC:  outColor = vec4(vec3(metallic), 0.0); return;
    case DEBUG_ROUGHNESS: outColor = vec4(vec3(roughness), 0.0); return;
    case DEBUG_AO:        outColor = vec4(vec3(ao), 0.0); return;
    case DEBUG_INDIRECT:  outColor = vec4(toFilmic(indirectDiffuse + indirectSpecular), 0.0); return;
    default: break;
    }

    vec3 color = light.rgb + indirectDiffuse + indirectSpecular;
    // Alpha is an SSR F0/mask, not generic dielectric reflectivity. Sponza is
    // forced non-metallic, so only the explicitly authored mirror pool traces.
    vec3 cspecSsr = mix(vec3(0.04), albedo, metallic);
    float reflectivity = metallic * dot(cspecSsr, vec3(0.2126, 0.7152, 0.0722));

    outColor = vec4(color, reflectivity);
}
