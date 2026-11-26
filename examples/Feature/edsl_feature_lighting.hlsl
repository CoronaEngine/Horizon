float3 shadeBox(float3 albedo, float3 normal) {
    float3 lightDirection = normalize(float3(0.5, 0.8, 0.6));
    float diffuse = saturate(dot(normal, lightDirection));
    return albedo * (0.25 + 0.75 * diffuse);
}
