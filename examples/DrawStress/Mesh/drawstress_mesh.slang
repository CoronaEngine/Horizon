struct DrawParameters {
    column_major float4x4 mvp;
};

[[vk::binding(0, 1)]] StructuredBuffer<DrawParameters> transforms;

struct Vertex {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

static const uint3 triangles[12] = {
    uint3(0, 1, 2), uint3(1, 3, 2), uint3(4, 6, 5), uint3(5, 6, 7),
    uint3(0, 2, 4), uint3(4, 2, 6), uint3(1, 5, 3), uint3(5, 7, 3),
    uint3(0, 4, 1), uint3(4, 5, 1), uint3(2, 3, 6), uint3(6, 3, 7),
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(12, 1, 1)]
void main(uint lane : SV_GroupIndex, uint drawID : SV_DrawIndex,
          out vertices Vertex vertices[8], out indices uint3 indices[12]) {
    SetMeshOutputCounts(8, 12);
    if (lane < 8) {
        float3 color = float3(lane & 1, (lane >> 1) & 1, (lane >> 2) & 1);
        float3 position = color * float3(2, -2, -2) + float3(-1, 1, 1);
        vertices[lane].position = mul(transforms[drawID].mvp, float4(position, 1.0));
        vertices[lane].color = color;
    }
    indices[lane] = triangles[lane];
}
