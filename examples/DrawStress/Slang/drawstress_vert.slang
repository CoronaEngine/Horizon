#include "../../raster_batch.slang"

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
