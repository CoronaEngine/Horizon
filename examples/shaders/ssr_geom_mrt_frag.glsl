#version 450

// G-buffer MRT fragment shader（SSR/SSSR 版）。
// 直接光照升级为完整 Disney Principled BRDF（对齐 example_disney_pbr），
// 所有非 albedo 的材质参数从共享 UBO fsp.disney_a/b/c 读取，
// 仅 albedo 随 per-draw push constant vpc_fs.material.rgb 变化。

layout(set = 3, binding = 0) uniform SsrGeomShared {
    mat4 view_proj;
    mat4 view;
    vec4 light_dir_vs;  // xyz: view 空间指向光源的方向, w: 环境光强度
    vec4 disney_a;      // x=metallic, y=roughness, z=specular, w=specular_tint
    vec4 disney_b;      // x=subsurface, y=anisotropic, z=sheen, w=sheen_tint
    vec4 disney_c;      // x=clearcoat, y=clearcoat_gloss
} fsp;

// vert/frag 共享同一 push constant 块（布局一致）。
layout(push_constant) uniform SsrGeomPC {
    mat4 model;
    vec4 material;  // rgb: albedo, w: 未用
} vpc_fs;

layout(location = 0) in vec3 v_normal_vs;
layout(location = 1) in vec3 v_pos_vs;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outDepthVal;
layout(location = 3) out vec4 outAlbedoMet;

const float PI = 3.14159265358979323846;

float sqr(float x) { return x * x; }

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

// view 空间切线帧（同 disney_pbr_frag.glsl，用 world-up 在 VS 空间近似代替）
void buildAnisoFrame(vec3 N, out vec3 X, out vec3 Y)
{
    X = normalize(cross(N, vec3(0.0, 1.0, 0.0)));
    if (dot(X, X) < 1e-4)
        X = normalize(cross(N, vec3(1.0, 0.0, 0.0)));
    Y = normalize(cross(N, X));
}

void main()
{
    vec3 n = normalize(v_normal_vs);
    vec3 l = normalize(fsp.light_dir_vs.xyz);
    vec3 v = normalize(-v_pos_vs); // 相机在 view 空间原点

    vec3 albedo      = vpc_fs.material.rgb;
    float metallic   = fsp.disney_a.x;
    float roughness  = clamp(fsp.disney_a.y, 0.001, 1.0);
    float specular   = fsp.disney_a.z;
    float specTint   = fsp.disney_a.w;
    float subsurface = fsp.disney_b.x;
    float aniso      = fsp.disney_b.y;
    float sheen      = fsp.disney_b.z;
    float sheenTint  = fsp.disney_b.w;
    float clearcoat  = fsp.disney_c.x;
    float ccGloss    = fsp.disney_c.y;

    // 颜色推导（同 disney_pbr_frag.glsl）
    vec3 Cdlin = pow(abs(albedo), vec3(2.2)); // sRGB → linear
    float Cdlum = dot(Cdlin, vec3(0.3, 0.6, 0.1));
    vec3 Ctint = Cdlum > 0.0 ? Cdlin / Cdlum : vec3(1.0);
    vec3 Cspec0 = mix(specular * 0.08 * mix(vec3(1.0), Ctint, specTint), Cdlin, metallic);
    vec3 Csheen = mix(vec3(1.0), Ctint, sheenTint);

    vec3 X, Y;
    buildAnisoFrame(n, X, Y);

    float NdotL = dot(n, l);
    float NdotV = dot(n, v);

    vec3 lit = vec3(0.0);
    if (NdotL >= 0.0 && NdotV >= 0.0)
    {
        vec3 h     = normalize(l + v);
        float NdotH = dot(n, h);
        float LdotH = dot(l, h);

        float FL  = SchlickFresnel(NdotL);
        float FV  = SchlickFresnel(NdotV);
        float Fd90 = 0.5 + 2.0 * LdotH * LdotH * roughness;
        float Fd   = mix(1.0, Fd90, FL) * mix(1.0, Fd90, FV);

        float Fss90 = LdotH * LdotH * roughness;
        float Fss   = mix(1.0, Fss90, FL) * mix(1.0, Fss90, FV);
        float ss    = 1.25 * (Fss * (1.0 / (NdotL + NdotV) - 0.5) + 0.5);

        float aspect = sqrt(1.0 - aniso * 0.9);
        float ax = max(0.001, sqr(roughness) / aspect);
        float ay = max(0.001, sqr(roughness) * aspect);
        float Ds = GTR2_aniso(NdotH, dot(h, X), dot(h, Y), ax, ay);
        float FH = SchlickFresnel(LdotH);
        vec3  Fs = mix(Cspec0, vec3(1.0), FH);
        float Gs = smithG_GGX_aniso(NdotL, dot(l, X), dot(l, Y), ax, ay)
                 * smithG_GGX_aniso(NdotV, dot(v, X), dot(v, Y), ax, ay);

        vec3 Fsheen = FH * sheen * Csheen;

        float Dr = GTR1(NdotH, mix(0.1, 0.001, ccGloss));
        float Fr = mix(0.04, 1.0, FH);
        float Gr = smithG_GGX(NdotL, 0.25) * smithG_GGX(NdotV, 0.25);

        vec3 diffPart = ((1.0 / PI) * mix(Fd, ss, subsurface) * Cdlin + Fsheen) * (1.0 - metallic);
        vec3 specPart = Gs * Fs * Ds + 0.25 * clearcoat * Gr * Fr * Dr;
        lit = (diffPart + specPart) * NdotL;
    }

    // 环境光项（ambient）：albedo * ambient 强度
    lit += albedo * fsp.light_dir_vs.w;

    outColor     = vec4(lit, 1.0);            // a=1: 标记「有几何」（清屏为 0，trace 跳过天空）
    outNormal    = vec4(n * 0.5 + 0.5, roughness);
    outDepthVal  = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
    outAlbedoMet = vec4(albedo, metallic);    // trace 重建 Disney 材质用
}
