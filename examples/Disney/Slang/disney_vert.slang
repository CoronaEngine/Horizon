#include "disney_shared.slang"

[[vk::binding(0, 3)]] ConstantBuffer<DisneyPbrShared> vsp;

[shader("vertex")]
DisneyVaryings main(uint vertexID : SV_VertexID, uint drawID : SV_DrawIndex) {
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + drawID];
    uint* vertex = hzPullVertex(packet, vertexID);
    float3 inPosition = *(float3*)vertex;
    DisneyDraw* draw = getDisneyDraw(packet.parameterWord);

    DisneyVaryings output;
    output.parameterWord = packet.parameterWord;
    float4x4 mvp = mul(vsp.proj_view, draw->model);
    output.position = mul(mvp, float4(inPosition, 1.0));
    float3 worldPos = mul(draw->model, float4(inPosition, 1.0)).xyz;
    output.view = vsp.camPos.xyz - worldPos;
    output.normal = mul(draw->model, float4(inPosition, 0.0)).xyz;
    return output;
}
