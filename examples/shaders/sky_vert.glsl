#version 450

// Port of bgfx examples/36-sky vs_sky.sc (Perez sky on a screen-space grid).
// set 0-2 are Horizon bindless reserved sets; ordinary UBO goes at set 3.

layout(set = 3, binding = 0) uniform SkyShared {
    mat4 invViewProj;
    vec4 sunDirection;
    vec4 skyLuminanceXYZ;
    vec4 sunLuminance;
    vec4 parameters; // x: sun size, y: sun bloom, z: exposure, w: time
    // Separate fields (not an array) so Helicon binding codegen emits valid C++ names.
    vec4 perez0;
    vec4 perez1;
    vec4 perez2;
    vec4 perez3;
    vec4 perez4;
} sky;

layout(location = 0) in vec2 inPosition;

layout(location = 0) out vec3 v_skyColor;
layout(location = 1) out vec2 v_screenPos;
layout(location = 2) out vec3 v_viewDir;

vec3 convertXYZ2RGB(vec3 xyz)
{
    return vec3(
        dot(vec3(3.240479, -1.537150, -0.498530), xyz),
        dot(vec3(-0.969256, 1.875991, 0.041556), xyz),
        dot(vec3(0.055648, -0.204043, 1.057311), xyz));
}

vec3 Perez(vec3 A, vec3 B, vec3 C, vec3 D, vec3 E, float costeta, float cosgamma)
{
    float invCosteta = 1.0 / costeta;
    float cos2gamma = cosgamma * cosgamma;
    float gamma = acos(cosgamma);
    vec3 f = (vec3(1.0) + A * exp(B * invCosteta))
           * (vec3(1.0) + C * exp(D * gamma) + E * cos2gamma);
    return f;
}

void main()
{
    v_screenPos = inPosition;

    vec4 rayStart = sky.invViewProj * vec4(inPosition, -1.0, 1.0);
    vec4 rayEnd = sky.invViewProj * vec4(inPosition, 1.0, 1.0);
    rayStart /= rayStart.w;
    rayEnd /= rayEnd.w;

    v_viewDir = normalize(rayEnd.xyz - rayStart.xyz);
    v_viewDir.y = abs(v_viewDir.y);

    // Far plane; sky is drawn first with depth write disabled.
    gl_Position = vec4(inPosition, 1.0, 1.0);

    vec3 lightDir = normalize(sky.sunDirection.xyz);
    vec3 skyDir = vec3(0.0, 1.0, 0.0);

    vec3 A = sky.perez0.xyz;
    vec3 B = sky.perez1.xyz;
    vec3 C = sky.perez2.xyz;
    vec3 D = sky.perez3.xyz;
    vec3 E = sky.perez4.xyz;

    float costeta = max(dot(v_viewDir, skyDir), 0.001);
    float cosgamma = clamp(dot(v_viewDir, lightDir), -0.9999, 0.9999);
    float cosgammas = dot(skyDir, lightDir);

    vec3 P = Perez(A, B, C, D, E, costeta, cosgamma);
    vec3 P0 = Perez(A, B, C, D, E, 1.0, cosgammas);

    vec3 skyColorxyY = vec3(
        sky.skyLuminanceXYZ.x / (sky.skyLuminanceXYZ.x + sky.skyLuminanceXYZ.y + sky.skyLuminanceXYZ.z),
        sky.skyLuminanceXYZ.y / (sky.skyLuminanceXYZ.x + sky.skyLuminanceXYZ.y + sky.skyLuminanceXYZ.z),
        sky.skyLuminanceXYZ.y);

    vec3 Yp = skyColorxyY * P / P0;
    vec3 skyColorXYZ = vec3(Yp.x * Yp.z / Yp.y, Yp.z, (1.0 - Yp.x - Yp.y) * Yp.z / Yp.y);

    v_skyColor = convertXYZ2RGB(skyColorXYZ * sky.parameters.z);
}
