// These layouts match the raster draw packet, geometry table, and push constants documented in README.md.
struct HorizonDrawPacket {
    uint vertexCount, instanceCount, firstVertex, firstInstance;
    uint geometryIndex, firstIndex;
    int baseVertex;
    uint parameterWord;
};
struct HorizonGeometry {
    uint* vertices;
    uint* indices;
    uint strideWords, indexBytes, vertexCount, reserved;
};
struct HorizonRasterRoot {
    HorizonDrawPacket* packets;
    HorizonGeometry* geometries;
    uint* parameters;
    uint groupBase;
};
[[vk::push_constant]] ConstantBuffer<HorizonRasterRoot> drawRoot;

uint hzVertexIndex(HorizonGeometry geometry, HorizonDrawPacket packet, uint corner) {
    uint element = packet.firstIndex + corner;
    uint pair = geometry.indices[element >> 1];
    uint index = (pair >> (16 * (element & 1))) & 0xffff;
    return index + uint(packet.baseVertex);
}

struct DrawParameters {
    column_major float4x4 mvp;
};

struct VertexOutput {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
VertexOutput main(uint vertexID : SV_VertexID, uint drawID : SV_DrawIndex) {
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + drawID];
    uint corner = hzVertexIndex(drawRoot.geometries[packet.geometryIndex], packet, vertexID);
    float3 color = float3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
    float3 position = color * float3(2, -2, -2) + float3(-1, 1, 1);
    DrawParameters* draw = (DrawParameters*)(drawRoot.parameters + packet.parameterWord);

    VertexOutput output;
    output.position = mul(draw->mvp, float4(position, 1.0));
    output.color = color;
    return output;
}
