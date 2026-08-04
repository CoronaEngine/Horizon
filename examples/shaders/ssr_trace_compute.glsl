#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

// 屏幕空间随机反射（SSSR）射线步进 compute shader。
// sampleDisneyBRDF 已升级为完整 Disney Principled BRDF：
//   - Fresnel F0 使用 specular/specularTint 参数（对齐 disney_pbr_frag.glsl）
//   - 新增 clearcoat 第三 lobe（GTR1 分布，固定粗糙度 0.25）
//   - sheen 项叠加进 diffuse 吞吐
//   - anisotropic 各向异性 GGX（切线帧从 view 空间法线推导）
//   - subsurface 影响 diffuse 形状
// metallic/roughness 仍从 G-buffer（albedo_met.a / normal.a）逐像素读取；
// 其余 Disney 参数从 push_constants.disney_mat1/2 读取（批次共享）。
//
// Push constant 布局（128 bytes 精确填满）：
//   uint×5 ids + uint×3 pad   = 32 B
//   vec4 ndc_to_view          = 16 B
//   vec4 params0              = 16 B  x=p00,y=p11,z=maxDist,w=numSteps
//   vec4 params1              = 16 B  x=thickness,y=frameIdx,z=w=res
//   vec4 params2              = 16 B  x=refineSteps,y=useJitter,z=specular,w=subsurface
//   vec4 disney_mat1          = 16 B  x=specTint,y=aniso,z=sheen,w=sheenTint
//   vec4 disney_mat2          = 16 B  x=clearcoat,y=ccGloss,zw=0
//   Total                     = 128 B ✓

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 2, binding = 0, r32f)    uniform image2D imagesR32F[];
layout(set = 2, binding = 0, rgba8)   uniform image2D imagesRGBA8[];
layout(set = 2, binding = 0, rgba16f) uniform image2D imagesRGBA16F[];

layout(push_constant) uniform PushConsts
{
    uint linearDepthID; // R32F   view 空间线性深度
    uint normalID;      // RGBA8  xyz: view 法线, w: roughness
    uint colorID;       // RGBA16F xyz: 直接光结果, w: 几何标记
    uint ssrID;         // RGBA16F xyz: 弹射贡献, w: 置信度（本站输出）
    uint albedoMetID;   // RGBA8  xyz: albedo, w: metallic
    uint pad0a;
    uint pad0b;
    uint pad0c;
    vec4 ndc_to_view;   // xy: mul, zw: add（uv → view.xy / viewZ）
    vec4 params0;       // x: proj00, y: proj11, z: maxDistance, w: numSteps
    vec4 params1;       // x: thickness, y: frameIdx, z: width, w: height
    vec4 params2;       // x: refineSteps, y: useJitter, z: specular, w: subsurface
    vec4 disney_mat1;   // x: specularTint, y: anisotropic, z: sheen, w: sheenTint
    vec4 disney_mat2;   // x: clearcoat, y: clearcoatGloss, zw: 未用
} pushConsts;

#define DEPTH_EPSILON 1e-4

vec2 view_to_uv(vec3 p)
{
    vec2 ndc = vec2(pushConsts.params0.x * p.x, pushConsts.params0.y * p.y) / p.z;
    return ndc * 0.5 + 0.5;
}

float load_linear_depth(vec2 uv)
{
    ivec2 c = ivec2(uv * pushConsts.params1.zw);
    c = clamp(c, ivec2(0), ivec2(pushConsts.params1.zw) - 1);
    return imageLoad(imagesR32F[pushConsts.linearDepthID], c).x;
}

float interleaved_gradient_noise(vec2 p)
{
    const vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(p, magic.xy)));
}

// ===================================================================
// 完整 Disney BRDF lobe 采样工具箱
// ===================================================================
const float PI = 3.14159265359;
float g_seed;
float frand()
{
    g_seed = fract(sin(g_seed * 91.3458 + 47.9898) * 43758.5453123);
    return g_seed;
}

float sqr(float x) { return x * x; }
float SchlickFresnel(float u)
{
    float m = clamp(1.0 - u, 0.0, 1.0);
    return m * m * m * m * m;
}
float luma3(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// GTR1：clearcoat 分布
float GTR1(float NdotH, float a)
{
    if (a >= 1.0) return 1.0 / PI;
    float a2 = a * a;
    float t = 1.0 + (a2 - 1.0) * NdotH * NdotH;
    return (a2 - 1.0) / (PI * log(a2) * t);
}

// 各向异性 GGX（用于 specular lobe）
float GTR2_aniso(float NdotH, float HdotX, float HdotY, float ax, float ay)
{
    return 1.0 / (PI * ax * ay * sqr(sqr(HdotX / ax) + sqr(HdotY / ay) + NdotH * NdotH));
}

float smithG_GGX(float NdotV, float alphaG)
{
    float a = alphaG * alphaG, b = NdotV * NdotV;
    return 1.0 / (NdotV + sqrt(a + b - a * b));
}

float smithG_GGX_aniso(float NdotV, float VdotX, float VdotY, float ax, float ay)
{
    return 1.0 / (NdotV + sqrt(sqr(VdotX * ax) + sqr(VdotY * ay) + sqr(NdotV)));
}

void basis(vec3 n, out vec3 t, out vec3 b)
{
    vec3 up = abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

// VNDF 采样：GGX 各向异性半向量（局部坐标系）
vec3 sampleGGXVNDF_aniso(vec3 Vl, float ax, float ay, float r1, float r2)
{
    vec3 Vh = normalize(vec3(ax * Vl.x, ay * Vl.y, Vl.z));
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    vec3 T1 = lensq > 1e-7 ? vec3(-Vh.y, Vh.x, 0.0) / sqrt(lensq) : vec3(1.0, 0.0, 0.0);
    vec3 T2 = cross(Vh, T1);
    float r = sqrt(r1);
    float phi = 2.0 * PI * r2;
    float t1 = r * cos(phi), t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(max(0.0, 1.0 - t1 * t1)) + s * t2;
    vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;
    return normalize(vec3(ax * Nh.x, ay * Nh.y, max(0.0, Nh.z)));
}

vec3 cosineSampleHemisphere(vec3 n)
{
    float z = frand() * 2.0 - 1.0;
    float phi = frand() * 2.0 * PI;
    float r = sqrt(max(0.0, 1.0 - z * z));
    return normalize(n * 1.0001 + vec3(r * cos(phi), r * sin(phi), z));
}

// Disney BRDF 完整采样：3 个 lobe（diffuse / aniso-specular / clearcoat）
// 返回 rgb = brdf(l,v)*NoL, a = lobe_prob * pdf
// 输入：v=出射(观察), n=法线, albedo=线性颜色, metallic/roughness/其余参数
vec4 sampleDisneyBRDF(vec3 v, vec3 n,
                      vec3 albedo, float metallic, float roughness,
                      float specular, float specTint,
                      float subsurface, float aniso,
                      float sheen, float sheenTint,
                      float clearcoat, float ccGloss,
                      inout vec3 l)
{
    float a     = max(roughness * roughness, 1e-3);
    float aspect = sqrt(1.0 - aniso * 0.9);
    float ax    = max(0.001, sqr(roughness) / aspect);
    float ay    = max(0.001, sqr(roughness) * aspect);

    vec3 t, b;
    basis(n, t, b);
    vec3 Vl = vec3(dot(v, t), dot(v, b), dot(v, n));

    // Fresnel F0：完整 Disney 公式（含 specular / specTint）
    vec3 Cdlin = pow(abs(albedo), vec3(2.2));
    float Cdlum = max(luma3(Cdlin), 1e-4);
    vec3 Ctint  = Cdlin / Cdlum;
    vec3 Cspec0 = mix(specular * 0.08 * mix(vec3(1.0), Ctint, specTint), Cdlin, metallic);
    vec3 Csheen = mix(vec3(1.0), Ctint, sheenTint);

    // 采样 specular lobe 的代表半向量（各向异性 VNDF）
    vec3 h_local = sampleGGXVNDF_aniso(Vl, ax, ay, frand(), frand());
    if (h_local.z < 0.0) h_local = -h_local;
    vec3 h = t * h_local.x + b * h_local.y + n * h_local.z;

    vec3 Fr_h = Cspec0 + (vec3(1.0) - Cspec0) * SchlickFresnel(dot(v, h));
    float specW = luma3(Fr_h);
    float diffW = 1.0 - metallic;

    // clearcoat lobe 权重（固定 F0=0.04）
    float ccW = 0.25 * clearcoat; // 原版系数 0.25

    float totalW = diffW + specW + ccW;
    if (totalW < 1e-6) return vec4(0.0);
    float invW = 1.0 / totalW;
    diffW *= invW; specW *= invW; ccW *= invW;

    float rnd = frand();
    vec4 brdf = vec4(0.0);

    if (rnd < diffW)
    {
        // ── diffuse lobe：余弦半球采样 ──
        l = cosineSampleHemisphere(n);
        vec3 hh = normalize(l + v);
        float NoL = dot(n, l), NoV = dot(n, v);
        if (NoL <= 0.0 || NoV <= 0.0) return vec4(0.0);

        float LdotH = dot(l, hh);
        float pdf   = NoL / PI;
        float FD90  = 0.5 + 2.0 * roughness * LdotH * LdotH;
        float Fd    = (1.0 + (FD90 - 1.0) * SchlickFresnel(NoL))
                    * (1.0 + (FD90 - 1.0) * SchlickFresnel(NoV));

        float Fss90 = LdotH * LdotH * roughness;
        float Fss   = (1.0 + (Fss90 - 1.0) * SchlickFresnel(NoL))
                    * (1.0 + (Fss90 - 1.0) * SchlickFresnel(NoV));
        float ss    = 1.25 * (Fss * (1.0 / (NoL + NoV) - 0.5) + 0.5);

        vec3 diff = Cdlin * (1.0 / PI) * mix(Fd, ss, subsurface) * (1.0 - metallic);

        float FH    = SchlickFresnel(LdotH);
        vec3 Fsh    = FH * sheen * Csheen;

        vec3 Fr_l   = Cspec0 + (vec3(1.0) - Cspec0) * SchlickFresnel(LdotH);
        brdf.rgb    = (diff * (vec3(1.0) - Fr_l) + Fsh) * NoL;
        brdf.a      = diffW * pdf;
    }
    else if (rnd < diffW + specW)
    {
        // ── aniso-specular lobe：reflect(-v, h) ──
        l = reflect(-v, h);
        float NoL = dot(n, l), NoV = dot(n, v);
        if (NoL <= 0.0 || NoV <= 0.0) return vec4(0.0);

        float NoH = min(dot(n, h), 0.99);
        float pdf  = GTR2_aniso(NoH, dot(h, t), dot(h, b), ax, ay) * NoH
                   / max(4.0 * NoV, 1e-5);
        float Gs   = smithG_GGX_aniso(NoL, dot(l, t), dot(l, b), ax, ay)
                   * smithG_GGX_aniso(NoV, dot(v, t), dot(v, b), ax, ay);
        float Ds   = GTR2_aniso(NoH, dot(h, t), dot(h, b), ax, ay);

        brdf.rgb = Fr_h * Gs * Ds / max(4.0 * NoL * NoV, 1e-5) * NoL;
        brdf.a   = specW * pdf;
    }
    else
    {
        // ── clearcoat lobe：GTR1，各向同性，粗糙度固定 0.25 ──
        // 重新采样 clearcoat 半向量（各向同性 VNDF，alphaG=0.25）
        vec3 Vl_cc  = vec3(dot(v, t), dot(v, b), dot(v, n));
        float ac    = mix(0.1, 0.001, ccGloss);  // clearcoatAlpha
        vec3 h_cc_l = sampleGGXVNDF_aniso(Vl_cc, ac, ac, frand(), frand());
        if (h_cc_l.z < 0.0) h_cc_l = -h_cc_l;
        vec3 h_cc   = t * h_cc_l.x + b * h_cc_l.y + n * h_cc_l.z;

        l = reflect(-v, h_cc);
        float NoL = dot(n, l), NoV = dot(n, v);
        if (NoL <= 0.0 || NoV <= 0.0) return vec4(0.0);

        float NoH_cc = min(dot(n, h_cc), 0.99);
        float FH_cc  = SchlickFresnel(dot(l, h_cc));
        float Fr_cc  = mix(0.04, 1.0, FH_cc);
        float Dr_cc  = GTR1(NoH_cc, ac);
        float Gr_cc  = smithG_GGX(NoL, 0.25) * smithG_GGX(NoV, 0.25);
        float pdf_cc = Dr_cc * NoH_cc / max(4.0 * NoV, 1e-5);

        brdf.rgb = vec3(0.25 * clearcoat * Fr_cc * Gr_cc * Dr_cc / max(4.0 * NoL * NoV, 1e-5)) * NoL;
        brdf.a   = ccW * pdf_cc;
    }

    return brdf;
}

void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    vec2 resolution = pushConsts.params1.zw;
    if (coord.x >= int(resolution.x) || coord.y >= int(resolution.y))
        return;

    imageStore(imagesRGBA16F[pushConsts.ssrID], coord, vec4(0.0));

    float has_geo = imageLoad(imagesRGBA16F[pushConsts.colorID], coord).w;
    if (has_geo <= 0.5) return;

    float view_z   = imageLoad(imagesR32F[pushConsts.linearDepthID], coord).x;
    vec2  uv       = (vec2(coord) + 0.5) / resolution;
    vec3  view_pos = vec3((uv * pushConsts.ndc_to_view.xy + pushConsts.ndc_to_view.zw) * view_z, view_z);

    vec4  packed_normal = imageLoad(imagesRGBA8[pushConsts.normalID], coord);
    vec3  n             = normalize(packed_normal.xyz * 2.0 - 1.0);
    float roughness     = packed_normal.w;

    vec4  albedo_met = imageLoad(imagesRGBA8[pushConsts.albedoMetID], coord);
    vec3  albedo     = albedo_met.rgb;
    float metallic   = albedo_met.a;

    // 从 push constants 读取批次共享的 Disney 参数
    float specular   = pushConsts.params2.z;
    float subsurface = pushConsts.params2.w;
    float specTint   = pushConsts.disney_mat1.x;
    float aniso      = pushConsts.disney_mat1.y;
    float sheen      = pushConsts.disney_mat1.z;
    float sheenTint  = pushConsts.disney_mat1.w;
    float clearcoat  = pushConsts.disney_mat2.x;
    float ccGloss    = pushConsts.disney_mat2.y;

    g_seed = fract(sin(dot(vec2(coord), vec2(12.9898, 78.233))) * 43758.5453 +
                   pushConsts.params1.y * 0.6180339887);

    vec3 v  = normalize(view_pos);
    vec3 wo = -v;
    vec3 ray_dir = reflect(v, n); // 兜底
    vec4 brdf = sampleDisneyBRDF(wo, n, albedo, metallic, roughness,
                                 specular, specTint, subsurface, aniso,
                                 sheen, sheenTint, clearcoat, ccGloss,
                                 ray_dir);
    if (brdf.a <= 0.0) return;

    vec3 throughput = min(brdf.rgb / brdf.a, vec3(4.0));

    float num_steps = max(1.0, pushConsts.params0.w);
    float step_len  = pushConsts.params0.z / num_steps;
    vec3  ray_step  = ray_dir * step_len;
    vec3  sample_pos = view_pos + n * (step_len * 0.5);

    float random = interleaved_gradient_noise(vec2(coord) + vec2(314.0, 159.0) * pushConsts.params1.y);
    float initial_offset = (pushConsts.params2.y > 0.0) ? (0.5 + random) : 1.0;
    sample_pos += initial_offset * ray_step;

    float thickness = pushConsts.params1.x;
    bool hit = false;
    float hit_step = num_steps;
    vec3 prev_pos = sample_pos;

    for (int i = 0; i < int(num_steps); ++i, sample_pos += ray_step)
    {
        if (sample_pos.z <= 0.01) break;

        vec2 s_uv = view_to_uv(sample_pos);
        if (any(lessThan(s_uv, vec2(0.0))) || any(greaterThan(s_uv, vec2(1.0)))) break;

        float scene_z = load_linear_depth(s_uv);
        float delta   = sample_pos.z - scene_z;

        if (DEPTH_EPSILON < delta && delta < thickness)
        {
            hit = true; hit_step = float(i); break;
        }
        prev_pos = sample_pos;
    }

    if (!hit) return;

    vec3 lo = prev_pos, hi = sample_pos;
    for (int k = 0; k < int(pushConsts.params2.x); ++k)
    {
        vec3 mid = (lo + hi) * 0.5;
        if (mid.z > load_linear_depth(view_to_uv(mid))) hi = mid; else lo = mid;
    }
    vec2 hit_uv = view_to_uv(hi);

    ivec2 hit_coord  = clamp(ivec2(hit_uv * resolution), ivec2(0), ivec2(resolution) - 1);
    vec3  refl_color = imageLoad(imagesRGBA16F[pushConsts.colorID], hit_coord).xyz;

    vec2 fade_lo = smoothstep(vec2(0.0), vec2(0.12), hit_uv);
    vec2 fade_hi = vec2(1.0) - smoothstep(vec2(0.88), vec2(1.0), hit_uv);
    float edge_fade = fade_lo.x * fade_lo.y * fade_hi.x * fade_hi.y;
    float dist_fade = mix(0.45, 1.0, 1.0 - clamp(hit_step / num_steps, 0.0, 1.0));
    float confidence = edge_fade * dist_fade;

    imageStore(imagesRGBA16F[pushConsts.ssrID], coord,
               vec4(refl_color * throughput * confidence, clamp(confidence, 0.0, 1.0)));
}
