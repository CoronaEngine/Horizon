#include "BaselineMesh.h"
#include "../BaselineAssets.h"

BaselineMesh::BaselineMesh(void* native_window)
    : ExampleBase(native_window), display(native_window),
      color(800, 600, HardwareImage::Format::RGBA16_FLOAT),
      depth(800, 600, HardwareImage::Format::D32_FLOAT, HardwareImage::Usage::DepthImage),
      pipeline({.compare = RasterPipeline<>::Compare::LessEqual})
{
    const auto assets = loadBaselineAssets(executor, HardwareBuffer::Usage::StorageBuffer, HardwareBuffer::Usage::StorageBuffer);
    const auto triangles = static_cast<uint32_t>(assets.indices.getElementCount() / 3);
    const MeshPipeline<>::IndirectPacket draw{(triangles + 31) / 32, 1, 1};
    executor << groups.createDeviceLocal(HardwareBuffer::Usage::RasterIndirect, &draw, 1, sizeof(draw));

    pipeline.geometry[0] = assets.vertices;
    pipeline.geometry[1] = assets.indices;
    pipeline.vp.texIndex = assets.texture.storeSampledDescriptor();
    pipeline.vp.proj = baselineProjection();
    pipeline.outColor = color;
}

void BaselineMesh::run(float seconds)
{
    pipeline.vp.view = view * ktm::rotate3d_z(seconds * ktm::radians(90.0f));
    executor << pipeline(groups, 1, depth) << display(color);
    executor.commit();
}
