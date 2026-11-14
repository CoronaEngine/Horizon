#include "../raster_batch.slang"

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
    uint* vertex = hzPullVertex(packet, vertexID);
    DrawParameters* draw = (DrawParameters*)(drawRoot.parameters + packet.parameterWord);

    VertexOutput output;
    output.position = mul(draw->mvp, float4(*(float3*)vertex, 1.0));
    output.color = *(float3*)(vertex + 3);
    return output;
}
