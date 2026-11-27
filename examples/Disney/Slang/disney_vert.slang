#include "../disney_scene.slangh"

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

uint* hzPullVertex(HorizonDrawPacket packet, uint corner) {
    HorizonGeometry geometry = drawRoot.geometries[packet.geometryIndex];
    return geometry.vertices + hzVertexIndex(geometry, packet, corner) * geometry.strideWords;
}

[shader("vertex")]
DisneyVaryings main(uint vertexID : SV_VertexID, uint drawID : SV_DrawIndex) {
    HorizonDrawPacket packet = drawRoot.packets[drawRoot.groupBase + drawID];
    uint* vertex = hzPullVertex(packet, vertexID);
    float3 inPosition = *(float3*)vertex;
    DisneyDraw* draw = (DisneyDraw*)(drawRoot.parameters + packet.parameterWord);
    return sphereVertex(inPosition, draw->model, packet.parameterWord / (sizeof(DisneyDraw) / 4));
}
