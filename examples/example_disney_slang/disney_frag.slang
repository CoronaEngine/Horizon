#include "disney_shared.slang"

// Disney Principled BRDF (wdas/brdf disney.brdf) + IBL from environment maps.
[[vk::binding(0, 3)]] ConstantBuffer<DisneyPbrShared> fsp;
[[vk::binding(0, 0)]] SamplerCube<float4> combinedCubeSamplerHandles[];

static const float PI = 3.14159265358979323846;

float sqr(float x) { return x * x; }

float3 toLinear(float3 rgb) { return pow(abs(rgb), float3(2.2)); }

float3 toFilmic(float3 rgb)
{
    rgb = max(float3(0.0), rgb - 0.004);
    rgb = (rgb * (6.2 * rgb + 0.5)) / (rgb * (6.2 * rgb + 1.7) + 0.06);
    return rgb;
}

float3 fixCubeLookup(float3 v, float lod, float topLevelCubeSize)
{
    float ax = abs(v.x), ay = abs(v.y), az = abs(v.z);
    float vmax = max(max(ax, ay), az);
    float scale = 1.0 - exp2(lod) / topLevelCubeSize;
    if (ax != vmax) { v.x *= scale; }
    if (ay != vmax) { v.y *= scale; }
    if (az != vmax) { v.z *= scale; }
    return v;
}

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

void buildAnisoFrame(float3 N, out float3 X, out float3 Y)
{
    X = normalize(cross(N, float3(0.0, 1.0, 0.0)));
    if (dot(X, X) < 1e-4)
        X = normalize(cross(N, float3(1.0, 0.0, 0.0)));
    Y = normalize(cross(N, X));
}

float3 evalDisneyDirect(DisneyDraw* draw, float3 L, float3 V, float3 N, float3 X, float3 Y)
{
    float NdotL = dot(N, L);
    float NdotV = dot(N, V);
    if (NdotL < 0.0 || NdotV < 0.0)
        return float3(0.0);

    float3 H = normalize(L + V);
    float NdotH = dot(N, H);
    float LdotH = dot(L, H);

    float3 baseColor = draw->mat0.rgb;
    float metallic = draw->mat0.a;
    float roughness = clamp(draw->mat1.x, 0.001, 1.0);
    float specular = draw->mat1.y;
    float specularTint = draw->mat1.z;
    float subsurface = draw->mat1.w;
    float anisotropic = draw->mat2.x;
    float sheen = draw->mat2.y;
    float sheenTint = draw->mat2.z;
    float clearcoat = draw->mat2.w;
    float clearcoatGloss = draw->clearcoatGloss;

    float3 Cdlin = toLinear(baseColor);
    float Cdlum = dot(Cdlin, float3(0.3, 0.6, 0.1));
    float3 Ctint = Cdlum > 0.0 ? Cdlin / Cdlum : float3(1.0);
    float3 Cspec0 = lerp(specular * 0.08 * lerp(float3(1.0), Ctint, specularTint), Cdlin, metallic);
    float3 Csheen = lerp(float3(1.0), Ctint, sheenTint);

    float FL = SchlickFresnel(NdotL);
    float FV = SchlickFresnel(NdotV);
    float Fd90 = 0.5 + 2.0 * LdotH * LdotH * roughness;
    float Fd = lerp(1.0, Fd90, FL) * lerp(1.0, Fd90, FV);

    float Fss90 = LdotH * LdotH * roughness;
    float Fss = lerp(1.0, Fss90, FL) * lerp(1.0, Fss90, FV);
    float ss = 1.25 * (Fss * (1.0 / (NdotL + NdotV) - 0.5) + 0.5);

    float aspect = sqrt(1.0 - anisotropic * 0.9);
    float ax = max(0.001, sqr(roughness) / aspect);
    float ay = max(0.001, sqr(roughness) * aspect);
    float Ds = GTR2_aniso(NdotH, dot(H, X), dot(H, Y), ax, ay);
    float FH = SchlickFresnel(LdotH);
    float3 Fs = lerp(Cspec0, float3(1.0), FH);
    float Gs = smithG_GGX_aniso(NdotL, dot(L, X), dot(L, Y), ax, ay);
    Gs *= smithG_GGX_aniso(NdotV, dot(V, X), dot(V, Y), ax, ay);

    float3 Fsheen = FH * sheen * Csheen;

    float Dr = GTR1(NdotH, lerp(0.1, 0.001, clearcoatGloss));
    float Fr = lerp(0.04, 1.0, FH);
    float Gr = smithG_GGX(NdotL, 0.25) * smithG_GGX(NdotV, 0.25);

    float3 diffusePart = ((1.0 / PI) * lerp(Fd, ss, subsurface) * Cdlin + Fsheen) * (1.0 - metallic);
    float3 specPart = Gs * Fs * Ds + 0.25 * clearcoat * Gr * Fr * Dr;
    return diffusePart + specPart;
}

[shader("fragment")]
void main(DisneyVaryings input, out float4 outColor : SV_Target0)
{
    DisneyDraw* draw = getDisneyDraw(input.parameterWord);
    float3 N = normalize(input.normal);
    float3 V = normalize(input.view);
    float3 L = normalize(fsp.lightDir.xyz);
    float3 X;
    float3 Y;
    buildAnisoFrame(N, X, Y);

    float3 baseColor = draw->mat0.rgb;
    float metallic = draw->mat0.a;
    float roughness = clamp(draw->mat1.x, 0.001, 1.0);
    float specular = draw->mat1.y;
    float specularTint = draw->mat1.z;

    float3 Cdlin = toLinear(baseColor);
    float Cdlum = dot(Cdlin, float3(0.3, 0.6, 0.1));
    float3 Ctint = Cdlum > 0.0 ? Cdlin / Cdlum : float3(1.0);
    float3 Cspec0 = lerp(specular * 0.08 * lerp(float3(1.0), Ctint, specularTint), Cdlin, metallic);

    float NdotV = clamp(dot(N, V), 0.0, 1.0);
    float envFresnel = lerp(Cspec0.r, 1.0, SchlickFresnel(NdotV)); // scalar approx for mip env

    float3 direct = float3(0.0);
    if (fsp.flags.x > 0.5 || fsp.flags.y > 0.5)
    {
        float3 brdf = evalDisneyDirect(draw, L, V, N, X, Y);
        float NdotL = clamp(dot(N, L), 0.0, 1.0);
        float3 lit = brdf * NdotL * fsp.lightCol.xyz;
        direct = lit * max(fsp.flags.x + fsp.flags.y, 0.0);
    }

    float3 indirect = float3(0.0);
    if (fsp.flags.z > 0.5 || fsp.flags.w > 0.5)
    {
        float mip = roughness * 5.0;
        float3 vr = normalize(reflect(-V, N));
        float3 cubeR = normalize(mul(fsp.envMtx, float4(vr, 0.0)).xyz);
        float3 cubeN = normalize(mul(fsp.envMtx, float4(N, 0.0)).xyz);
        cubeR = fixCubeLookup(cubeR, mip, 256.0);

        float3 radiance = toLinear(combinedCubeSamplerHandles[draw->texCubeIndex].SampleLevel(cubeR, mip).xyz);
        float3 irradiance = toLinear(combinedCubeSamplerHandles[draw->texCubeIrrIndex].Sample(cubeN).xyz);

        float3 diffuseAlbedo = Cdlin * (1.0 - metallic);
        float3 envDiffuse = diffuseAlbedo * irradiance * fsp.flags.z;
        float3 envSpecular = Cspec0 * radiance * envFresnel * fsp.flags.w;
        indirect = envDiffuse + envSpecular;
    }

    float3 color = (direct + indirect) * exp2(fsp.exposurePad.x);
    outColor = float4(toFilmic(color), 1.0);
}
