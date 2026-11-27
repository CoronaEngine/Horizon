struct Payload { float3 color; };

[shader("miss")]
void main(inout Payload payload) {
    payload.color = float3(0.0);
}
