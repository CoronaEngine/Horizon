#version 450

// 移植自 bgfx examples/18-ibl 的 fs_ibl_mesh.sc + fs_ibl_skybox.sc。
// uniform block 与 ibl_vert.glsl 完全一致（共用 binding=0，见 vert 注释）。

layout(binding = 0) uniform IblShared {
    mat4 proj_view;
    mat4 skyEnvMtx;
    mat4 envMtx;
    vec4 camPos;
    vec4 flags;
    vec4 rgbDiff;
    vec4 rgbSpec;
    vec4 lightDir;
    vec4 lightCol;
} fsp;

layout(push_constant) uniform IblPC {
    mat4 model;
    vec4 params0;  // x: glossiness, y: reflectivity, z: exposure, w: bgType
    vec4 misc;     // x: isSkybox, y: aspect, z: metalOrSpec
} fpc;

layout(binding = 2) uniform samplerCube texCube;
layout(binding = 3) uniform samplerCube texCubeIrr;

layout(location = 0) in vec3 v_view;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec3 v_dir;

layout(location = 0) out vec4 outColor;

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

vec3 calcFresnel(vec3 cspec, float dotNV, float strength)
{
    return cspec + (1.0 - cspec) * pow(1.0 - dotNV, 5.0) * strength;
}
vec3 calcLambert(vec3 cdiff, float ndotl) { return cdiff * ndotl; }
vec3 calcBlinn(vec3 cspec, float ndoth, float ndotl, float specPower)
{
    float norm = (specPower + 8.0) * 0.125;
    float brdf = pow(ndoth, specPower) * ndotl * norm;
    return cspec * brdf;
}
float specPwr(float gloss) { return exp2(10.0 * gloss + 2.0); }

void main()
{
    if (fpc.misc.x > 0.5)
    {
        // ---- 天空盒 ----
        vec3 dir = normalize(v_dir);
        float bgType = fpc.params0.w;

        vec3 color;
        if (bgType == 7.0)
            color = toLinear(texture(texCubeIrr, dir).xyz);
        else
        {
            float lod = bgType;
            dir = fixCubeLookup(dir, lod, 256.0);
            color = toLinear(textureLod(texCube, dir, lod).xyz);
        }
        color *= exp2(fpc.params0.z);
        outColor = vec4(toFilmic(color), 1.0);
        return;
    }

    // ---- IBL 网格着色 ----
    vec3 ld     = normalize(fsp.lightDir.xyz);
    vec3 clight = fsp.lightCol.xyz;

    vec3 nn = normalize(v_normal);
    vec3 vv = normalize(v_view);
    vec3 hh = normalize(vv + ld);

    float ndotv = clamp(dot(nn, vv), 0.0, 1.0);
    float ndotl = clamp(dot(nn, ld), 0.0, 1.0);
    float ndoth = clamp(dot(nn, hh), 0.0, 1.0);
    float hdotv = clamp(dot(hh, vv), 0.0, 1.0);

    vec3  inAlbedo       = fsp.rgbDiff.xyz;
    float inReflectivity = fpc.params0.y;
    float inGloss        = fpc.params0.x;

    vec3 refl;
    if (fpc.misc.z == 0.0)
        refl = mix(vec3(0.04), inAlbedo, inReflectivity);
    else
        refl = fsp.rgbSpec.xyz * vec3(inReflectivity);

    vec3 albedo     = inAlbedo * (1.0 - inReflectivity);
    vec3 dirFresnel = calcFresnel(refl, hdotv, inGloss);
    vec3 envFresnel = calcFresnel(refl, ndotv, inGloss);

    vec3 lambert = fsp.flags.x * calcLambert(albedo * (1.0 - dirFresnel), ndotl);
    vec3 blinn   = fsp.flags.y * calcBlinn(dirFresnel, ndoth, ndotl, specPwr(inGloss));
    vec3 direct  = (lambert + blinn) * clight;

    float mip = 1.0 + 5.0 * (1.0 - inGloss);

    vec3 vr = 2.0 * ndotv * nn - vv;
    vec3 cubeR = normalize((fsp.envMtx * vec4(vr, 0.0)).xyz);
    vec3 cubeN = normalize((fsp.envMtx * vec4(nn, 0.0)).xyz);
    cubeR = fixCubeLookup(cubeR, mip, 256.0);

    vec3 radiance   = toLinear(textureLod(texCube, cubeR, mip).xyz);
    vec3 irradiance = toLinear(texture(texCubeIrr, cubeN).xyz);
    vec3 envDiffuse  = albedo     * irradiance * fsp.flags.z;
    vec3 envSpecular = envFresnel * radiance   * fsp.flags.w;
    vec3 indirect    = envDiffuse + envSpecular;

    vec3 color = (direct + indirect) * exp2(fpc.params0.z);
    outColor = vec4(toFilmic(color), 1.0);
}
