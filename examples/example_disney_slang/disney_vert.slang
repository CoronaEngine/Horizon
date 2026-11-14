#include "disney_shared.slang"

[[vk::binding(0, 3)]] ConstantBuffer<DisneyPbrShared> vsp;

[shader("vertex")]
DisneyVaryings main(uint vertexID : SV_VertexID, uint drawID : SV_DrawIndex) {
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + drawID];
    uint* vertex = hzPullVertex(packet, vertexID);
    float3 inPosition = *(float3*)vertex;
    float3 inNormal = *(float3*)(vertex + 3);
    DisneyDraw* draw = getDisneyDraw(packet.parameterWord);

    DisneyVaryings output;
    output.parameterWord = packet.parameterWord;
    if (draw->mat3.y > 0.5) {
        output.position = float4(inPosition.xy, 0.999999, 1.0);
        float fovHeight = tan(radians(45.0) * 0.5);
        float2 tex = inPosition.xy * float2(fovHeight * draw->mat3.z, -fovHeight);
        output.dir = mul(vsp.skyEnvMtx, float4(tex, 1.0, 0.0)).xyz;
        output.view = float3(0.0);
        output.normal = float3(0.0, 0.0, 1.0);
    } else {
        float4x4 mvp = mul(vsp.proj_view, draw->model);
        output.position = mul(mvp, float4(inPosition, 1.0));
        float3 worldPos = mul(draw->model, float4(inPosition, 1.0)).xyz;
        output.view = vsp.camPos.xyz - worldPos;
        output.normal = mul(draw->model, float4(inNormal, 0.0)).xyz;
        output.dir = float3(0.0);
    }
    return output;
}
