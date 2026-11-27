struct ViewProj {
    column_major float4x4 view;
    column_major float4x4 proj;
    uint texIndex;
};
[[vk::binding(0, 3)]] ConstantBuffer<ViewProj> vp;
[[vk::binding(0, 1)]] ByteAddressBuffer geometry[2]; // Packed xyz/uv vertices and uint indices.

struct VertexOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void main(uint lane : SV_GroupIndex, uint3 group : SV_GroupID,
          out vertices VertexOutput vertices[96], out indices uint3 triangles[32]) {
    uint indexBytes;
    geometry[1].GetDimensions(indexBytes);
    uint triangleCount = indexBytes / 12;
    uint first = group.x * 32;
    uint count = first < triangleCount ? min(triangleCount - first, 32u) : 0u;
    SetMeshOutputCounts(count * 3, count);
    if (lane >= count) return;

    uint3 indices = geometry[1].Load3((first + lane) * 12);
    for (uint corner = 0; corner < 3; ++corner) {
        uint address = indices[corner] * 20;
        float3 position = asfloat(geometry[0].Load3(address));
        uint vertex = lane * 3 + corner;
        vertices[vertex].position = mul(vp.proj, mul(vp.view, float4(position, 1.0)));
        vertices[vertex].texCoord = asfloat(geometry[0].Load2(address + 12));
    }
    triangles[lane] = lane * 3 + uint3(0, 1, 2);
}
