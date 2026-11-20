#include "baseline_draw.slang"

struct ViewProj {
    column_major float4x4 view;
    column_major float4x4 proj;
};
[[vk::binding(0, 3)]] ConstantBuffer<ViewProj> vp;

struct VertexOutput {
    float4 position : SV_Position;
    float3 color : COLOR0;
    float2 texCoord : TEXCOORD0;
    nointerpolation uint parameterWord : TEXCOORD1;
};

[shader("vertex")]
VertexOutput main(uint vertexID : SV_VertexID, uint drawID : SV_DrawIndex) {
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + drawID];
    uint* vertex = hzPullVertex(packet, vertexID);
    float3 position = *(float3*)vertex;
    float4x4 model = getDrawParameters(packet.parameterWord)->model;

    VertexOutput output;
    float4x4 mvp = mul(mul(vp.proj, vp.view), model);
    output.position = mul(mvp, float4(position, 1.0));
    output.color = float3(1.0);
    output.texCoord = *(float2*)(vertex + 3);
    output.parameterWord = packet.parameterWord;
    return output;
}
