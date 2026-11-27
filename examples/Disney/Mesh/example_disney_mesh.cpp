#include "DisneyMesh.h"

#include <array>

DisneyMesh::DisneyMesh(void* native_window)
    : ExampleBase(native_window), scene(executor), display(native_window)
{
    // One indirect draw per material. Each workgroup emits up to 16 quads in a latitude band.
    std::array<MeshPipeline<>::IndirectPacket, DisneyScene::rows * DisneyScene::columns> packets;
    packets.fill({(DisneyScene::slices + 15) / 16, DisneyScene::stacks, 1});
    executor << commands.createDeviceLocal(HardwareBuffer::Usage::RasterIndirect,
                                           packets.data(), packets.size(), sizeof(packets[0]));
    scene.bind(pipeline);
    pipeline.sphere.slices = uint32_t(DisneyScene::slices);
    pipeline.sphere.stacks = uint32_t(DisneyScene::stacks);
}

void DisneyMesh::run(float)
{
    executor << pipeline(commands, DisneyScene::rows * DisneyScene::columns, scene.depth) << display(scene.color);
    executor.commit();
}
