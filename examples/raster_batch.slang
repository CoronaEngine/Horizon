// Layout used by the handwritten examples. See the raster shader contract in README.md.
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
