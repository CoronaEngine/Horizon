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

uint* hzPullVertex(HorizonDrawPacket packet, uint corner) {
    HorizonGeometry geometry = drawRoot.geometries[packet.geometryIndex];
    uint vertex = geometry.indices[packet.firstIndex + corner] + uint(packet.baseVertex);
    return geometry.vertices + vertex * geometry.strideWords;
}

// Matches the GLM matrix and texture index uploaded for each draw.
struct DrawParameters {
    column_major float4x4 model;
    uint texIndex;
};

struct ViewProj {
    column_major float4x4 view;
    column_major float4x4 proj;
};
[[vk::binding(0, 3)]] ConstantBuffer<ViewProj> vp;

struct VertexOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    nointerpolation uint parameterWord : TEXCOORD1;
};

[shader("vertex")]
VertexOutput main(uint vertexID : SV_VertexID, uint drawID : SV_DrawIndex) {
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + drawID];
    uint* vertex = hzPullVertex(packet, vertexID);
    float3 position = *(float3*)vertex;
    DrawParameters* draw = (DrawParameters*)(drawRoot.parameters + packet.parameterWord);

    VertexOutput output;
    float4x4 mvp = mul(mul(vp.proj, vp.view), draw->model);
    output.position = mul(mvp, float4(position, 1.0));
    output.texCoord = *(float2*)(vertex + 3);
    output.parameterWord = packet.parameterWord;
    return output;
}
