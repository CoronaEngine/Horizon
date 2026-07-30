#version 450

// Sponza deferred light accumulation, fragment stage —— Disney BRDF + RSM。
//
// 相比旧版(Blinn-Phong、diffuse 存 rgb / spec 存 a、combine 里再乘 albedo),
// 这里直接在光照 pass 里算完 Disney Principled BRDF(移植自
// disney_pbr_frag.glsl,wdas/brdf disney.brdf)的完整直接光 radiance:
//   - baseColor/metallic 来自 G-buffer albedo(.a=metallic)
//   - roughness 来自 G-buffer normal.a
//   - 其余 Disney 参数(specular/sheen/clearcoat/...)是全场景 imgui 滑条
// 输出 rgb = 完整直接光 radiance(已含 albedo),a = 该光的亮度(调试用)。
// 累加仍是 additive blend,radiance 线性可加。
//
// 太阳(平行光)分支额外做 RSM 单次弹射间接光(Dachsbacher & Stamminger 2005):
// 把着色点投到太阳的正交 RSM 上,在周围重要性采样 32 个像素光,
//   E_p = Φ_p · max(0,⟨n_p|x-x_p⟩)·max(0,⟨n|x_p-x⟩) / ‖x-x_p‖⁴
// RSM 像素的世界坐标由 (uv, depth) 经 inv_sun_view_proj 重建,省一张 position 图。

#extension GL_EXT_nonuniform_qualifier : enable

// set 0-2 are reserved for Horizon bindless tables, so plain UBOs go in set 3.
layout(set = 3, binding = 0) uniform SponzaLightShared {
    mat4 inv_view_proj;
    mat4 view;
    vec4 camera_pos;
    mat4 sun_view_proj;
    mat4 inv_sun_view_proj;
    vec4 shadow_params; // x: world size of one shadow texel, y: depth bias,
                        // z: shadow map resolution, w: 1 to enable sun shadows
    vec4 rsm_params;    // x: 采样半径(uv), y: 间接光强度, z: 1 启用 RSM,
                        // w: 近距钳制 d²(场景尺度相关,防 firefly)
    vec4 disney0;       // x: specular, y: specularTint, z: sheen, w: sheenTint
    vec4 disney1;       // x: clearcoat, y: clearcoatGloss, z: subsurface, w: anisotropic
} fsp;

// Shares the push constant block with sponza_light_vert.glsl.
layout(push_constant) uniform SponzaLightPC {
    vec4 light_pos_radius;
    vec4 light_rgb_inner_r;
    vec4 rect;
    uint gAlbedoIndex;
    uint gNormalIndex;
    uint gDepthIndex;
    uint gShadowIndex;
    uint rsmNormalIndex;
    uint rsmFluxIndex;
} fpc;

layout(set = 0, binding = 0) uniform sampler2D combinedTextureSamplerHandles[];

#define gAlbedo   combinedTextureSamplerHandles[fpc.gAlbedoIndex]
#define gNormal   combinedTextureSamplerHandles[fpc.gNormalIndex]
#define gDepth    combinedTextureSamplerHandles[fpc.gDepthIndex]
#define gShadow   combinedTextureSamplerHandles[fpc.gShadowIndex]
#define rsmNormal combinedTextureSamplerHandles[fpc.rsmNormalIndex]
#define rsmFlux   combinedTextureSamplerHandles[fpc.rsmFluxIndex]

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265358979323846;
const int RSM_SAMPLE_COUNT = 32;
const float TWO_PI = 6.28318530718;
const float GOLDEN = 0.61803398875;

float sqr(float x) { return x * x; }

// ============================================================================
// Disney Principled BRDF(移植自 disney_pbr_frag.glsl)
// ============================================================================

float SchlickFresnel(float u)
{
    float m = clamp(1.0 - u, 0.0, 1.0);
    return m * m * m * m * m;
}

float GTR1(float NdotH, float a)
{
    if (a >= 1.0) return 1.0 / PI;
    float a2 = a * a;
    float t = 1.0 + (a2 - 1.0) * NdotH * NdotH;
    return (a2 - 1.0) / (PI * log(a2) * t);
}

float GTR2_aniso(float NdotH, float HdotX, float HdotY, float ax, float ay)
{
    return 1.0 / (PI * ax * ay * sqr(sqr(HdotX / ax) + sqr(HdotY / ay) + NdotH * NdotH));
}

float smithG_GGX(float NdotV, float alphaG)
{
    float a = alphaG * alphaG;
    float b = NdotV * NdotV;
    return 1.0 / (NdotV + sqrt(a + b - a * b));
}

float smithG_GGX_aniso(float NdotV, float VdotX, float VdotY, float ax, float ay)
{
    return 1.0 / (NdotV + sqrt(sqr(VdotX * ax) + sqr(VdotY * ay) + sqr(NdotV)));
}

void buildAnisoFrame(vec3 N, out vec3 X, out vec3 Y)
{
    // normalize(0) is undefined. The old code normalized first and tested
    // afterwards, so horizontal floors/ceilings could feed NaNs into Disney.
    vec3 candidate = cross(N, vec3(0.0, 1.0, 0.0));
    if (dot(candidate, candidate) < 1e-6)
        candidate = cross(N, vec3(1.0, 0.0, 0.0));
    X = normalize(candidate);
    Y = normalize(cross(N, X));
}

// Cdlin: 线性 baseColor。返回未乘 NdotL/光色的 BRDF 值。
vec3 evalDisney(vec3 L, vec3 V, vec3 N, vec3 Cdlin, float metallic, float roughness)
{
    float NdotL = dot(N, L);
    float NdotV = dot(N, V);
    if (NdotL <= 0.0 || NdotV <= 0.0)
        return vec3(0.0);

    vec3 X, Y;
    buildAnisoFrame(N, X, Y);

    vec3 halfVector = L + V;
    if (dot(halfVector, halfVector) < 1e-8)
        return vec3(0.0);
    vec3 H = normalize(halfVector);
    float NdotH = clamp(dot(N, H), 0.0, 1.0);
    float LdotH = clamp(dot(L, H), 0.0, 1.0);

    float specular = fsp.disney0.x;
    float specularTint = fsp.disney0.y;
    float sheen = fsp.disney0.z;
    float sheenTint = fsp.disney0.w;
    float clearcoat = fsp.disney1.x;
    float clearcoatGloss = fsp.disney1.y;
    float subsurface = fsp.disney1.z;
    float anisotropic = fsp.disney1.w;

    float Cdlum = dot(Cdlin, vec3(0.3, 0.6, 0.1));
    vec3 Ctint = Cdlum > 0.0 ? Cdlin / Cdlum : vec3(1.0);
    vec3 Cspec0 = mix(specular * 0.08 * mix(vec3(1.0), Ctint, specularTint), Cdlin, metallic);
    vec3 Csheen = mix(vec3(1.0), Ctint, sheenTint);

    float FL = SchlickFresnel(NdotL);
    float FV = SchlickFresnel(NdotV);
    float Fd90 = 0.5 + 2.0 * LdotH * LdotH * roughness;
    float Fd = mix(1.0, Fd90, FL) * mix(1.0, Fd90, FV);

    float Fss90 = LdotH * LdotH * roughness;
    float Fss = mix(1.0, Fss90, FL) * mix(1.0, Fss90, FV);
    float ss = 1.25 * (Fss * (1.0 / (NdotL + NdotV) - 0.5) + 0.5);

    float aspect = sqrt(1.0 - anisotropic * 0.9);
    float ax = max(0.001, sqr(roughness) / aspect);
    float ay = max(0.001, sqr(roughness) * aspect);
    float Ds = GTR2_aniso(NdotH, dot(H, X), dot(H, Y), ax, ay);
    float FH = SchlickFresnel(LdotH);
    vec3 Fs = mix(Cspec0, vec3(1.0), FH);
    float Gs = smithG_GGX_aniso(NdotL, dot(L, X), dot(L, Y), ax, ay);
    Gs *= smithG_GGX_aniso(NdotV, dot(V, X), dot(V, Y), ax, ay);

    vec3 Fsheen = FH * sheen * Csheen;

    float Dr = GTR1(NdotH, mix(0.1, 0.001, clearcoatGloss));
    float Fr = mix(0.04, 1.0, FH);
    float Gr = smithG_GGX(NdotL, 0.25) * smithG_GGX(NdotV, 0.25);

    // Project policy: authored Sponza geometry is diffuse-only (metallic = 0).
    // Sheen is a grazing specular lobe, so it is also restricted to the
    // explicitly authored metallic mirror instead of making stone look coated.
    vec3 diffusePart = (1.0 / PI) * mix(Fd, ss, subsurface) * Cdlin * (1.0 - metallic);
    vec3 specPart = Gs * Fs * Ds + 0.25 * clearcoat * Gr * Fr * Dr;
    return diffusePart + (specPart + Fsheen) * metallic;
}

// ============================================================================
// 太阳阴影(3x3 PCF)与 RSM gather
// ============================================================================

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

// RSM texel 的世界坐标:正交投影下由 (uv, depth) 反投影。
vec3 rsmWorldPos(vec2 uv, float depth)
{
    vec4 wpos = fsp.inv_sun_view_proj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    return wpos.xyz / wpos.w;
}

// 单次弹射间接光。返回入射 irradiance(尚未乘接收面 albedo/π)。
vec3 rsmIndirect(vec3 x, vec3 n, vec3 sunColorLinear, vec3 sunTravelDirection)
{
    if (fsp.rsm_params.z < 0.5)
        return vec3(0.0);

    vec4 clip = fsp.sun_view_proj * vec4(x, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 center_uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(center_uv, vec2(0.0))) || any(greaterThan(center_uv, vec2(1.0))))
        return vec3(0.0);

    vec3 sum = vec3(0.0);
    for (int i = 0; i < RSM_SAMPLE_COUNT; ++i)
    {
        float xi1 = (float(i) + 0.5) / float(RSM_SAMPLE_COUNT);
        float xi2 = fract(float(i) * GOLDEN);
        vec2 uv = center_uv + fsp.rsm_params.x * xi1 * vec2(cos(TWO_PI * xi2), sin(TWO_PI * xi2));
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
            continue;

        float depth = texture(gShadow, uv).r;
        if (depth >= 1.0)
            continue; // 该 texel 没有几何

        vec3 xp = rsmWorldPos(uv, depth);
        vec3 np = normalize(texture(rsmNormal, uv).xyz * 2.0 - 1.0);
        // The base-color texture was sampled through an sRGB image view in the
        // RSM pass, so rsmFlux already stores linear albedo. Include the cosine
        // of the original sun illumination; omitting it made back-facing VPLs
        // bounce as much energy as sun-facing ones.
        float sourceNdotL = max(0.0, dot(np, -sunTravelDirection));
        vec3 flux = texture(rsmFlux, uv).rgb * sunColorLinear * sourceNdotL;

        vec3 w = x - xp;                 // 像素光 -> 着色点(未归一化,d² 折进分母)
        float d2 = max(dot(w, w), fsp.rsm_params.w); // 近距钳制防 firefly(按场景尺度)
        float e = max(0.0, dot(np, w)) * max(0.0, dot(n, -w)) / (d2 * d2);
        sum += flux * (e * xi1 * xi1);   // ξ1² 补偿中心密集的采样分布
    }
    // Keep the control independent of sample count. The remaining multiplier
    // is an artistic RSM gain because a texel's projected area is not stored.
    return sum * (fsp.rsm_params.y / float(RSM_SAMPLE_COUNT));
}

void main()
{
    vec4 albedoSample = texture(gAlbedo, v_texcoord);
    vec4 packedNormal = texture(gNormal, v_texcoord);
    float deviceDepth = texture(gDepth, v_texcoord).x;
    if (deviceDepth >= 1.0)
        discard; // nothing was rasterised here

    // baseColor KTX textures use BC7_SRGB. Sampling converted them to linear
    // before the G-buffer write, so applying pow(2.2) here was a second,
    // incorrect linearization that crushed the material response.
    vec3 Cdlin = albedoSample.rgb;
    float metallic = albedoSample.a;
    vec3 normal = normalize(packedNormal.xyz * 2.0 - 1.0);
    float roughness = clamp(packedNormal.w, 0.03, 1.0);

    vec3 clip = vec3(v_texcoord * 2.0 - 1.0, deviceDepth);
    vec4 wpos4 = fsp.inv_view_proj * vec4(clip, 1.0);
    vec3 wpos = wpos4.xyz / wpos4.w;

    vec3 viewDir = normalize(fsp.camera_pos.xyz - wpos);

    vec3 lightDir;
    float attenuation;
    vec3 indirect = vec3(0.0);
    if (fpc.light_pos_radius.w < 0.0)
    {
        // Directional sun: xyz is the direction the light travels.
        lightDir = -normalize(fpc.light_pos_radius.xyz);
        attenuation = sunShadow(wpos, normal);
        // RSM 单次弹射:E × 接收面 diffuse albedo / π(只作用于 diffuse)
        indirect = rsmIndirect(wpos, normal, fpc.light_rgb_inner_r.xyz,
                               normalize(fpc.light_pos_radius.xyz))
                   * Cdlin * (1.0 - metallic) / PI;
    }
    else
    {
        vec3 lp = fpc.light_pos_radius.xyz - wpos;
        attenuation = 1.0 - smoothstep(fpc.light_rgb_inner_r.w, 1.0,
                                       length(lp) / fpc.light_pos_radius.w);
        lightDir = normalize(lp);
    }

    float ndotl = max(0.0, dot(normal, lightDir));
    vec3 brdf = evalDisney(lightDir, viewDir, normal, Cdlin, metallic, roughness);
    vec3 radiance = brdf * ndotl * attenuation * fpc.light_rgb_inner_r.xyz + indirect;

    outColor = vec4(radiance, dot(radiance, vec3(0.299, 0.587, 0.114)));
}
