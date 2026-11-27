struct Payload { float3 color; };

[shader("closesthit")]
void main(inout Payload payload, BuiltInTriangleIntersectionAttributes hit) {
    // The mesh shader maps the unit cube's corners to RGB in local space.
    float3 p = ObjectRayOrigin() + RayTCurrent() * ObjectRayDirection();
    payload.color = saturate(0.5 + p * float3(0.5, -0.5, -0.5));
}
