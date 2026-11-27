#include "DrawStressMesh.h"

#include <vector>

DrawStressMesh::DrawStressMesh(void* native_window)
    : ExampleBase(native_window), display(native_window),
      color(800, 600, HardwareImage::Format::RGBA16_FLOAT),
      depth(800, 600, HardwareImage::Format::D32_FLOAT, HardwareImage::Usage::DepthImage)
{
    // One indirect draw per cube, with its transform indexed by SV_DrawIndex.
    const std::vector<MeshPipeline<>::IndirectPacket> packets(DrawStressTransforms::cube_count, {1, 1, 1});
    executor << commands.createDeviceLocal(HardwareBuffer::Usage::RasterIndirect,
                                           packets.data(), packets.size(), sizeof(packets[0]));
    color.setClearColor(0.19f, 0.19f, 0.19f, 1.0f);
    pipeline.outColor = color;
}

void DrawStressMesh::run(float seconds)
{
    pipeline.transforms = transforms.update(seconds, executor);
    executor << pipeline(commands, DrawStressTransforms::cube_count, depth) << display(color);
    executor.commit();
}
