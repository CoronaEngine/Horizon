#version 450

// Disney Principled BRDF (wdas/brdf disney.brdf) + IBL from example_ibl environment maps.

#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 3, binding = 0) uniform DisneyPbrShared {
    mat4 proj_view;
    mat4 skyEnvMtx;
    mat4 envMtx;
    vec4 camPos;
    vec4 flags;
    vec4 lightDir;
    vec4 lightCol;
    vec4 exposurePad;
} fsp;

layout(push_constant) uniform DisneyPbrPC {
    mat4 model;
    vec4 mat0;
    vec4 mat1;
    vec4 mat2;
    vec4 mat3;
    uint texCubeIndex;
    uint texCubeIrrIndex;
} fpc;

layout(set = 0, binding = 0) uniform samplerCube combinedCubeSamplerHandles[];

#define texCube    combinedCubeSamplerHandles[fpc.texCubeIndex]
#define texCubeIrr combinedCubeSamplerHandles[fpc.texCubeIrrIndex]

layout(location = 0) in vec3 v_view;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec3 v_dir;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265358979323846;

float sqr(float x) { return x * x; }

vec3 toLinear(vec3 rgb) { return pow(abs(rgb), vec3(2.2)); }

vec3 toFilmic(vec3 rgb)
{
    rgb = max(vec3(0.0), rgb - 0.004);
    rgb = (rgb * (6.2 * rgb + 0.5)) / (rgb * (6.2 * rgb + 1.7) + 0.06);
    return rgb;
}

vec3 fixCubeLookup(vec3 v, float lod, float topLevelCubeSize)
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

void buildAnisoFrame(vec3 N, out vec3 X, out vec3 Y)
{
    X = normalize(cross(N, vec3(0.0, 1.0, 0.0)));
    if (dot(X, X) < 1e-4)
        X = normalize(cross(N, vec3(1.0, 0.0, 0.0)));
    Y = normalize(cross(N, X));
}

vec3 evalDisneyDirect(vec3 L, vec3 V, vec3 N, vec3 X, vec3 Y)
{
    float NdotL = dot(N, L);
    float NdotV = dot(N, V);
    if (NdotL < 0.0 || NdotV < 0.0)
        return vec3(0.0);

    vec3 H = normalize(L + V);
    float NdotH = dot(N, H);
    float LdotH = dot(L, H);

    vec3 baseColor = fpc.mat0.rgb;
    float metallic = fpc.mat0.a;
    float roughness = clamp(fpc.mat1.x, 0.001, 1.0);
    float specular = fpc.mat1.y;
    float specularTint = fpc.mat1.z;
    float subsurface = fpc.mat1.w;
    float anisotropic = fpc.mat2.x;
    float sheen = fpc.mat2.y;
    float sheenTint = fpc.mat2.z;
    float clearcoat = fpc.mat2.w;
    float clearcoatGloss = fpc.mat3.x;

    vec3 Cdlin = toLinear(baseColor);
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

    vec3 diffusePart = ((1.0 / PI) * mix(Fd, ss, subsurface) * Cdlin + Fsheen) * (1.0 - metallic);
    vec3 specPart = Gs * Fs * Ds + 0.25 * clearcoat * Gr * Fr * Dr;
    return diffusePart + specPart;
}

void main()
{
    if (fpc.mat3.y > 0.5)
    {
        vec3 dir = normalize(v_dir);
        float bgType = fpc.mat3.w;

        vec3 color;
        if (bgType >= 6.5)
            color = toLinear(texture(texCubeIrr, dir).xyz);
        else
        {
            float lod = bgType;
            dir = fixCubeLookup(dir, lod, 256.0);
            color = toLinear(textureLod(texCube, dir, lod).xyz);
        }
        color *= exp2(fsp.exposurePad.x);
        outColor = vec4(toFilmic(color), 1.0);
        return;
    }

    vec3 N = normalize(v_normal);
    vec3 V = normalize(v_view);
    vec3 L = normalize(fsp.lightDir.xyz);
    vec3 X;
    vec3 Y;
    buildAnisoFrame(N, X, Y);

    vec3 baseColor = fpc.mat0.rgb;
    float metallic = fpc.mat0.a;
    float roughness = clamp(fpc.mat1.x, 0.001, 1.0);
    float specular = fpc.mat1.y;
    float specularTint = fpc.mat1.z;

    vec3 Cdlin = toLinear(baseColor);
    float Cdlum = dot(Cdlin, vec3(0.3, 0.6, 0.1));
    vec3 Ctint = Cdlum > 0.0 ? Cdlin / Cdlum : vec3(1.0);
    vec3 Cspec0 = mix(specular * 0.08 * mix(vec3(1.0), Ctint, specularTint), Cdlin, metallic);

    float NdotV = clamp(dot(N, V), 0.0, 1.0);
    float envFresnel = mix(Cspec0.r, 1.0, SchlickFresnel(NdotV)); // scalar approx for mip env

    vec3 direct = vec3(0.0);
    if (fsp.flags.x > 0.5 || fsp.flags.y > 0.5)
    {
        vec3 brdf = evalDisneyDirect(L, V, N, X, Y);
        float NdotL = clamp(dot(N, L), 0.0, 1.0);
        vec3 lit = brdf * NdotL * fsp.lightCol.xyz;
        direct = lit * max(fsp.flags.x + fsp.flags.y, 0.0);
    }

    vec3 indirect = vec3(0.0);
    if (fsp.flags.z > 0.5 || fsp.flags.w > 0.5)
    {
        float mip = roughness * 5.0;
        vec3 vr = normalize(reflect(-V, N));
        vec3 cubeR = normalize((fsp.envMtx * vec4(vr, 0.0)).xyz);
        vec3 cubeN = normalize((fsp.envMtx * vec4(N, 0.0)).xyz);
        cubeR = fixCubeLookup(cubeR, mip, 256.0);

        vec3 radiance = toLinear(textureLod(texCube, cubeR, mip).xyz);
        vec3 irradiance = toLinear(texture(texCubeIrr, cubeN).xyz);

        vec3 diffuseAlbedo = Cdlin * (1.0 - metallic);
        vec3 envDiffuse = diffuseAlbedo * irradiance * fsp.flags.z;
        vec3 envSpecular = Cspec0 * radiance * envFresnel * fsp.flags.w;
        indirect = envDiffuse + envSpecular;
    }

    vec3 color = (direct + indirect) * exp2(fsp.exposurePad.x);
    outColor = vec4(toFilmic(color), 1.0);
}
