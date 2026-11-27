struct Payload { float3 color; };
[[vk::binding(0, 1)]] StructuredBuffer<float2> cornerUVs;
[[vk::binding(0, 0)]] Sampler2D<float4> albedo;

[shader("closesthit")]
void main(inout Payload payload, BuiltInTriangleIntersectionAttributes hit) {
    uint corner = 3 * PrimitiveIndex();
    float2 uv = cornerUVs[corner] + hit.barycentrics.x * (cornerUVs[corner + 1] - cornerUVs[corner])
                                  + hit.barycentrics.y * (cornerUVs[corner + 2] - cornerUVs[corner]);
    payload.color = albedo.SampleLevel(uv, 0).rgb;
}
