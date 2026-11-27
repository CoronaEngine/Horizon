struct Camera { column_major float4x4 inverseViewProjection; };
struct Payload { float3 color; };

[[vk::push_constant]] ConstantBuffer<Camera> camera;
[[vk::binding(1, 3)]] RaytracingAccelerationStructure scene;
[[vk::binding(0, 2)]] RWTexture2D<float4> output;

[shader("raygeneration")]
void main() {
    uint2 pixel = DispatchRaysIndex().xy;
    float2 ndc = 2.0 * (float2(pixel) + 0.5) / float2(DispatchRaysDimensions().xy) - 1.0;
    float4 nearPoint = mul(camera.inverseViewProjection, float4(ndc, 0, 1));
    float4 farPoint = mul(camera.inverseViewProjection, float4(ndc, 1, 1));
    RayDesc ray;
    ray.Origin = nearPoint.xyz / nearPoint.w;
    ray.Direction = farPoint.xyz / farPoint.w - ray.Origin;
    ray.TMin = 0;
    ray.TMax = 1;

    Payload payload = {};
    TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 0, 0, ray, payload);
    output[pixel] = float4(payload.color, 1);
}
