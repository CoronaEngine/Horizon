struct Payload { float3 color; };
struct Material {
    float4 baseColorMetallic;
    float roughness, specular, specularTint, subsurface;
    float anisotropic, sheen, sheenTint, clearcoat;
    float clearcoatGloss, pad0, pad1, pad2;
};
[[vk::binding(0, 1)]] StructuredBuffer<Material> materials;
[[vk::binding(0, 0)]] SamplerCube<float4> environment[2];

[shader("closesthit")]
void main(inout Payload payload, BuiltInTriangleIntersectionAttributes hit) {
    const float pi = 3.141592653589793;
    Material m = materials[InstanceID()];
    float3 N = normalize(ObjectRayOrigin() + RayTCurrent() * ObjectRayDirection());
    float3 V = normalize(-WorldRayDirection());
    float3 L = normalize(float3(-0.35, 0.55, -0.75));
    float3 Cd = pow(abs(m.baseColorMetallic.rgb), 2.2);
    float metallic = m.baseColorMetallic.a;
    float luminance = dot(Cd, float3(0.3, 0.6, 0.1));
    float3 tint = luminance > 0 ? Cd / luminance : float3(1);
    float3 specColor = lerp(0.08 * m.specular * lerp(float3(1), tint, m.specularTint), Cd, metallic);
    float roughness = clamp(m.roughness, 0.001, 1.0);
    float nl = dot(N, L), nv = dot(N, V);
    float fv = pow(saturate(1 - nv), 5);
    float3 direct = 0;
    if (nl >= 0 && nv >= 0) {
        float3 X = cross(N, float3(0, 1, 0));
        if (dot(X, X) < 1e-4) X = cross(N, float3(1, 0, 0));
        X = normalize(X);
        float3 Y = normalize(cross(N, X));
        float3 H = normalize(L + V);
        float nh = dot(N, H), lh = dot(L, H);
        float fl = pow(saturate(1 - nl), 5), fh = pow(saturate(1 - lh), 5);

        float fd90 = 0.5 + 2 * lh * lh * roughness;
        float fd = lerp(1, fd90, fl) * lerp(1, fd90, fv);
        float fss90 = lh * lh * roughness;
        float fss = lerp(1, fss90, fl) * lerp(1, fss90, fv);
        float ss = 1.25 * (fss * (1 / (nl + nv) - 0.5) + 0.5);
        float3 sheen = fh * m.sheen * lerp(float3(1), tint, m.sheenTint);
        float3 diffuse = (Cd * lerp(fd, ss, m.subsurface) / pi + sheen) * (1 - metallic);

        float aspect = sqrt(1 - m.anisotropic * 0.9);
        float ax = max(0.001, roughness * roughness / aspect);
        float ay = max(0.001, roughness * roughness * aspect);
        float3 h = float3(dot(H, X) / ax, dot(H, Y) / ay, nh);
        float denom = dot(h, h);
        float ds = 1 / (pi * ax * ay * denom * denom);
        float gl = nl + length(float3(dot(L, X) * ax, dot(L, Y) * ay, nl));
        float gv = nv + length(float3(dot(V, X) * ax, dot(V, Y) * ay, nv));
        float3 specular = ds * lerp(specColor, float3(1), fh) / (gl * gv);

        float a = lerp(0.1, 0.001, m.clearcoatGloss), a2 = a * a;
        float dr = (a2 - 1) / (pi * log(a2) * (1 + (a2 - 1) * nh * nh));
        float grL = nl + sqrt(0.0625 + 0.9375 * nl * nl);
        float grV = nv + sqrt(0.0625 + 0.9375 * nv * nv);
        float clearcoat = 0.25 * m.clearcoat * dr * lerp(0.04, 1, fh) / (grL * grV);
        direct = 2 * saturate(nl) * (diffuse + specular + clearcoat);
    }

    float mip = roughness * 5;
    float3 R = normalize(reflect(-V, N));
    float3 ar = abs(R);
    float major = max(ar.x, max(ar.y, ar.z));
    float seamScale = 1 - exp2(mip) / 256;
    R *= float3(ar.x == major ? 1 : seamScale, ar.y == major ? 1 : seamScale, ar.z == major ? 1 : seamScale);
    float3 radiance = pow(abs(environment[0].SampleLevel(R, mip).rgb), 2.2);
    float3 irradiance = pow(abs(environment[1].SampleLevel(N, 0).rgb), 2.2);
    float3 rgb = direct + Cd * (1 - metallic) * irradiance + specColor * radiance * lerp(specColor.r, 1, fv);
    rgb = max(0, rgb - 0.004);
    payload.color = rgb * (6.2 * rgb + 0.5) / (rgb * (6.2 * rgb + 1.7) + 0.06);
}
