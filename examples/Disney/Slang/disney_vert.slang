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
    uint index;
    if (geometry.indexBytes == 2) {
        uint pair = geometry.indices[element >> 1];
        index = (pair >> (16 * (element & 1))) & 0xffff;
    } else {
        index = geometry.indices[element];
    }
    uint vertex = index + uint(packet.baseVertex);
    if (vertex >= geometry.vertexCount) vertex = 0;
    return vertex;
}

uint* hzPullVertex(HorizonDrawPacket packet, uint corner) {
    HorizonGeometry geometry = drawRoot.geometries[packet.geometryIndex];
    return geometry.vertices + hzVertexIndex(geometry, packet, corner) * geometry.strideWords;
}

struct DisneyVaryings {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 view : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
    [[vk::location(15)]] nointerpolation uint parameterWord : TEXCOORD15;
};

struct DisneyPbrShared {
    column_major float4x4 proj_view;
    column_major float4x4 envMtx;
    float4 camPos;
    float4 flags;       // Diffuse, specular, diffuse IBL, specular IBL
    float4 lightDir;
    float4 lightCol;
    float4 exposurePad; // x: exposure
};

struct DisneyDraw {
    column_major float4x4 model;
    float4 mat0; // baseColor.rgb, metallic
    float4 mat1; // roughness, specular, specularTint, subsurface
    float4 mat2; // anisotropic, sheen, sheenTint, clearcoat
    float clearcoatGloss;
    uint texCubeIndex;
    uint texCubeIrrIndex;
};

DisneyDraw* getDisneyDraw(uint parameterWord) {
    return (DisneyDraw*)(drawRoot.parameters + parameterWord);
}

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
