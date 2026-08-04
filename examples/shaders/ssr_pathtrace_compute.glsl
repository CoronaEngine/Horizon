#version 450
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

// Disney BRDF 路径追踪(移植自 Shadertoy 参考:GGX VNDF 重要性采样 + lobe 概率选择)。
// 场景改为解析求交(地板平面 + 4x4 球阵,粗糙度/金属度按格子轴分布),
// 与 EDSL 版逐行同构;差异声明:本版用运行期循环 + break/early-out,
// EDSL 版因无运行期循环为定长展开 + alive 掩码(同 ssr_trace 的既有不对称)。
//
// 输出:RGBA16F 直写最终输出图(不做时域累积:每帧固定弹跳数,工作量恒定,
// 帧序号只改噪声种子,适合作 GPU-bound 基准)。

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 2, binding = 0, rgba16f) uniform image2D imagesRGBA16F[];

layout(push_constant) uniform PushConsts
{
    uint outputID;      // RGBA16F 输出
    vec4 pt0;           // xyz: ro(相机位置), w: frameIdx
    vec4 pt1;           // xyz: cam_fwd, w: focal
    vec4 pt2;           // xyz: cam_right, w: width
    vec4 pt3;           // xyz: cam_up, w: height
} pushConsts;

const float PI = 3.14159265358979;
const int   BOUNCE_COUNT = 5;
const int   SPHERE_DIM = 4;      // 4x4 球阵
const float SPHERE_R = 1.0;
const vec3  SUN_DIR = normalize(vec3(5.0, 1.0, 0.0));

// ---------------------------------------------
// PRNG:浮点 hash 链(与 EDSL 版完全一致,避开未验证的 uint 位运算路径)
// ---------------------------------------------
float g_seed;

float frand()
{
    g_seed = fract(sin(g_seed * 91.3458 + 47.9898) * 43758.5453123);
    return g_seed;
}

// ---------------------------------------------
// 天空
// ---------------------------------------------
vec3 skyColor(vec3 rd)
{
    float t = clamp(rd.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = mix(vec3(0.55, 0.62, 0.75), vec3(0.15, 0.28, 0.55), t);
    float sun = pow(max(dot(rd, SUN_DIR), 0.0), 96.0);
    return sky * 1.1 + vec3(1.0, 0.85, 0.6) * sun * 8.0;
}

// ---------------------------------------------
// 场景:解析求交。material 打包:albedo + metallic + roughness
// ---------------------------------------------
struct Hit
{
    float t;
    vec3 n;
    vec3 albedo;
    float metallic;
    float roughness;
};

// 球心:x/z 均匀网格,y = SPHERE_R(落在地板上)
vec3 sphereCenter(int ix, int iz)
{
    return vec3(-4.5 + float(ix) * 3.0, SPHERE_R, -4.5 + float(iz) * 3.0);
}

Hit trace(vec3 ro, vec3 rd)
{
    Hit h;
    h.t = 1e30;

    // 地板 y=0:金属棋盘格(参考版 floor 分支:metallic=1, roughness 按格子交错)
    if (rd.y < -1e-6)
    {
        float t = -ro.y / rd.y;
        if (t > 0.001 && t < h.t)
        {
            vec3 p = ro + rd * t;
            h.t = t;
            h.n = vec3(0.0, 1.0, 0.0);
            h.albedo = vec3(0.75);
            h.metallic = 1.0;
            float checker = mod(floor(p.x) + floor(p.z), 2.0);
            h.roughness = checker * 0.25 + 0.25;
        }
    }

    // 球阵:roughness 沿 z 轴 0.05..0.95,metallic 沿 x 轴 0..1(参考版轴分布)
    for (int iz = 0; iz < SPHERE_DIM; ++iz)
    {
        for (int ix = 0; ix < SPHERE_DIM; ++ix)
        {
            vec3 c = sphereCenter(ix, iz);
            vec3 oc = ro - c;
            float b = dot(oc, rd);
            float c2 = dot(oc, oc) - SPHERE_R * SPHERE_R;
            float disc = b * b - c2;
            if (disc > 0.0)
            {
                float t = -b - sqrt(disc);
                if (t > 0.001 && t < h.t)
                {
                    h.t = t;
                    vec3 p = ro + rd * t;
                    h.n = normalize(p - c);
                    h.albedo = vec3(0.9);
                    h.metallic = float(ix) / 3.0;
                    h.roughness = 0.05 + (float(iz) / 3.0) * 0.9;
                }
            }
        }
    }

    return h;
}

// ---------------------------------------------
// BRDF 辅助(照搬参考实现)
// ---------------------------------------------
float schlickWeight(float u) { float m = clamp(1.0 - u, 0.0, 1.0); return m * m * m * m * m; }
float fschlick1(float f0, float f90, float u) { return f0 + (f90 - f0) * schlickWeight(u); }
vec3 fschlick3(vec3 f0, float u) { return f0 + (vec3(1.0) - f0) * schlickWeight(u); }

float dGTR2(float a, float NoH)
{
    float a2 = a * a;
    float t = 1.0 + (a2 - 1.0) * NoH * NoH;
    return a2 / (PI * t * t);
}

float smithG1(float x, float a2) { return 2.0 * x / (x + sqrt(a2 + (1.0 - a2) * x * x)); }
float geometryTerm(float NoL, float NoV, float a) { float a2 = a * a; return smithG1(NoL, a2) * smithG1(NoV, a2); }

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void basis(vec3 n, out vec3 t, out vec3 b)
{
    vec3 up = abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

vec3 toLocal(vec3 t, vec3 b, vec3 n, vec3 v) { return vec3(dot(v, t), dot(v, b), dot(v, n)); }
vec3 toWorld(vec3 t, vec3 b, vec3 n, vec3 v) { return t * v.x + b * v.y + n * v.z; }

// Heitz GGX VNDF 采样(各向同性 ax=ay=a)
vec3 sampleGGXVNDF(vec3 V, float a, float r1, float r2)
{
    vec3 Vh = normalize(vec3(a * V.x, a * V.y, V.z));
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    vec3 T1 = lensq > 1e-7 ? vec3(-Vh.y, Vh.x, 0.0) / sqrt(lensq) : vec3(1.0, 0.0, 0.0);
    vec3 T2 = cross(Vh, T1);
    float r = sqrt(r1);
    float phi = 2.0 * PI * r2;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;
    vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;
    return normalize(vec3(a * Nh.x, a * Nh.y, max(0.0, Nh.z)));
}

float ggxVNDFPdf(float NoH, float NoV, float a)
{
    float D = dGTR2(a, NoH);
    return D * NoH / max(4.0 * NoV, 1e-5);
}

vec3 cosineSampleHemisphere(vec3 n)
{
    float z = frand() * 2.0 - 1.0;
    float phi = frand() * 2.0 * PI;
    float r = sqrt(max(0.0, 1.0 - z * z));
    vec3 sp = vec3(r * cos(phi), r * sin(phi), z);
    return normalize(n * 1.0001 + sp);
}

// ---------------------------------------------
// Disney BRDF 采样(照搬参考的 lobe 概率结构)
// 返回 rgb = brdf*NoL, a = lobeW*pdf;out 参数 l 为下一方向
// ---------------------------------------------
vec4 sampleDisneyBRDF(vec3 v, vec3 n, vec3 albedo, float metallic, float roughness, inout vec3 l)
{
    float a = max(roughness * roughness, 1e-3);

    // 微表面法线
    vec3 t, b;
    basis(n, t, b);
    vec3 V = toLocal(t, b, n, v);
    vec3 h = sampleGGXVNDF(V, a, frand(), frand());
    if (h.z < 0.0) h = -h;
    h = toWorld(t, b, n, h);

    // Fresnel 与 lobe 权重
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fschlick3(f0, dot(v, h));
    float diffW = (1.0 - metallic);
    float specW = luma(F);
    float invW = 1.0 / (diffW + specW);
    diffW *= invW;
    specW *= invW;

    vec4 brdf = vec4(0.0);
    float rnd = frand();
    if (rnd < diffW) // diffuse
    {
        l = cosineSampleHemisphere(n);
        h = normalize(l + v);
        float NoL = dot(n, l);
        float NoV = dot(n, v);
        if (NoL <= 0.0 || NoV <= 0.0) { return vec4(0.0); }
        float LoH = dot(l, h);
        float pdf = NoL / PI;

        float FD90 = 0.5 + 2.0 * roughness * LoH * LoH;
        float fa = fschlick1(1.0, FD90, NoL);
        float fb = fschlick1(1.0, FD90, NoV);
        vec3 diff = albedo * (fa * fb / PI) * (vec3(1.0) - F);
        brdf.rgb = diff * NoL;
        brdf.a = diffW * pdf;
    }
    else // specular
    {
        l = reflect(-v, h);
        float NoL = dot(n, l);
        float NoV = dot(n, v);
        if (NoL <= 0.0 || NoV <= 0.0) { return vec4(0.0); }
        float NoH = min(dot(n, h), 0.99);
        float pdf = ggxVNDFPdf(NoH, NoV, a);

        float D = dGTR2(a, NoH);
        float G = geometryTerm(NoL, NoV, a);
        vec3 spec = D * F * G / max(4.0 * NoL * NoV, 1e-5);
        brdf.rgb = spec * NoL;
        brdf.a = specW * pdf;
    }

    return brdf;
}

// ---------------------------------------------
// 路径追踪主循环(照搬参考结构:sky break + 吸收 + 续方向)
// ---------------------------------------------
vec4 pathtrace(vec3 ro, vec3 rd)
{
    float firstDepth = 0.0;
    vec3 acc = vec3(0.0);
    vec3 abso = vec3(1.0);

    for (int i = 0; i < BOUNCE_COUNT; ++i)
    {
        Hit h = trace(ro, rd);
        if (i == 0) firstDepth = min(h.t, 1000.0);

        if (h.t > 999.0)
        {
            acc += skyColor(rd) * abso;
            break;
        }

        vec3 p = ro + rd * h.t;
        vec3 v = -rd;

        vec3 outDir = rd;
        vec4 brdf = sampleDisneyBRDF(v, h.n, h.albedo, h.metallic, h.roughness, outDir);

        if (brdf.a > 0.0)
            abso *= brdf.rgb / brdf.a;

        ro = p + h.n * 0.01;
        rd = outDir;
    }

    return vec4(acc, firstDepth);
}

void main()
{
    ivec2 tid = ivec2(gl_GlobalInvocationID.xy);
    float width = pushConsts.pt2.w;
    float height = pushConsts.pt3.w;
    if (tid.x >= int(width) || tid.y >= int(height)) return;

    // 种子:像素坐标 + 帧序号(与 EDSL 版一致)
    g_seed = fract(sin(dot(vec2(tid), vec2(12.9898, 78.233))) * 43758.5453 + pushConsts.pt0.w * 0.6180339887);

    // 光线:抖动像素中心 → 相机基向量组合(基在 CPU 端算好,免 shader 侧 cross)
    vec2 uv = (vec2(tid) + vec2(frand(), frand())) / vec2(width, height);
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.x *= width / height;
    ndc.y = -ndc.y; // Vulkan 图像 Y 向下
    vec3 rd = normalize(pushConsts.pt1.xyz * pushConsts.pt1.w + pushConsts.pt2.xyz * ndc.x + pushConsts.pt3.xyz * ndc.y);
    vec3 ro = pushConsts.pt0.xyz;

    vec4 col = pathtrace(ro, rd);

    // 雾(参考版:按首命中深度混天空色;exp 用 exp2 表达,与 EDSL 版一致)
    vec3 fogged = mix(col.rgb, skyColor(rd), 1.0 - exp2(-col.a * 0.004 * 1.442695));

    imageStore(imagesRGBA16F[pushConsts.outputID], tid, vec4(min(fogged, vec3(10.0)), 1.0));
}
